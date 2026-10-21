#include "Synth.h"

#include <algorithm>

namespace gmosh
{
//==============================================================================
void Synth::prepare (double sampleRateHz, int maxBlockSamples)
{
    sampleRate = sampleRateHz;

    voices.clear();
    voices.resize (static_cast<std::size_t> (kMaxVoices));
    for (auto& v : voices)
        v.prepare (sampleRateHz, maxBlockSamples);

    fx.prepare (sampleRateHz, maxBlockSamples);

    // Reserve so the audio thread never allocates (design doc section 10).
    pending.clear();
    pending.reserve (128);
    heldNotes.clear();
    heldNotes.reserve (static_cast<std::size_t> (kMaxVoices));

    scratchL.assign (static_cast<std::size_t> (maxBlockSamples) + 1, 0.0f);
    scratchR.assign (static_cast<std::size_t> (maxBlockSamples) + 1, 0.0f);
}

void Synth::releaseResources()
{
    fx.reset();
    heldNotes.clear();
    pending.clear();
}

//==============================================================================
void Synth::setPlayState (const PlayState& state)
{
    play = state;
}

//==============================================================================
void Synth::addNoteOn (int midiNote, float velocity, int sampleOffset)
{
    PendingNote e;
    e.note = midiNote;
    e.velocity = velocity;
    e.offset = sampleOffset;
    e.on = true;
    pending.push_back (e);
}

void Synth::addNoteOff (int midiNote, int sampleOffset)
{
    PendingNote e;
    e.note = midiNote;
    e.offset = sampleOffset;
    e.on = false;
    pending.push_back (e);
}

void Synth::allNotesOff()
{
    for (auto& v : voices)
        v.clearActive();

    heldNotes.clear();
    pending.clear();
    fx.reset();
}

//==============================================================================
int Synth::numActiveVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        if (v.isActive())
            ++n;
    return n;
}

//==============================================================================
int Synth::allocateVoice()
{
    for (std::size_t i = 0; i < voices.size(); ++i)
    {
        if (! voices[i].isActive())
            return static_cast<int> (i);
    }

    // Release aware stealing: take the quietest voice.
    int best = 0;
    float bestLevel = 1.0e9f;
    for (std::size_t i = 0; i < voices.size(); ++i)
    {
        const float level = voices[i].ampLevel();
        if (level < bestLevel)
        {
            bestLevel = level;
            best = static_cast<int> (i);
        }
    }

    voices[static_cast<std::size_t> (best)].release();
    return best;
}

//==============================================================================
void Synth::startVoice (int voiceIndex, int midiNote, float velocity)
{
    if (plant == nullptr || voiceIndex < 0)
        return;

    const PlantSnapshot& snapshot = plant->current();

    const int branch = branchForNote (midiNote, play.macros.rotation);
    const float distance = effectiveDistance (play.branchLength[static_cast<std::size_t> (branch)],
                                             play.macros.wheel, play.macros.wheelScale);

    BranchTraits traits;
    const Genome genome = resolveBranchGenome (snapshot, branch, distance, traits);

    VoiceParams params;
    resolveVoiceParams (genome, traits, play.macros, midiNote, velocity, params);

    const uint64_t voiceHash = hash::combine (snapshot.seedId,
                                              static_cast<uint64_t> (branch));
    voices[static_cast<std::size_t> (voiceIndex)].start (params, midiNote, voiceHash);

    lastPlayedBranch.store (branch);

    // The tail follows the timbre of what was just played (design doc 8).
    fx.setParams (params.fxSize, params.fxLength, params.fxDamp,
                  params.fxChorus, params.fxMix, params.fxWidth);
}

//==============================================================================
void Synth::applyPending (const PendingNote& event)
{
    if (event.on)
    {
        int voiceIndex = -1;
        for (auto& h : heldNotes)
        {
            if (h.note == event.note)
            {
                voiceIndex = h.voice;
                break;
            }
        }

        if (voiceIndex < 0)
            voiceIndex = allocateVoice();

        startVoice (voiceIndex, event.note, event.velocity);

        bool tracked = false;
        for (auto& h : heldNotes)
        {
            if (h.note < 0)
            {
                h.note = event.note;
                h.voice = voiceIndex;
                tracked = true;
                break;
            }
        }
        if (! tracked)
            heldNotes.push_back ({ event.note, voiceIndex });
    }
    else
    {
        for (auto& h : heldNotes)
        {
            if (h.note == event.note)
            {
                if (h.voice >= 0)
                    voices[static_cast<std::size_t> (h.voice)].release();
                h.note = -1;
                h.voice = -1;
                break;
            }
        }
    }
}

//==============================================================================
void Synth::renderSegment (float* const* channels, int numChannels, int start, int numSamples)
{
    if (numSamples <= 0)
        return;

    float* left = channels[0] + start;
    float* right = numChannels >= 2 ? channels[1] + start : scratchR.data() + start;

    if (numChannels < 2)
        left = scratchL.data() + start;

    for (auto& v : voices)
    {
        if (v.isActive())
            v.render (left, right, numSamples);
    }

    if (numChannels < 2)
    {
        float* mono = channels[0] + start;
        for (int i = 0; i < numSamples; ++i)
            mono[i] += (scratchL[static_cast<std::size_t> (start + i)]
                      + scratchR[static_cast<std::size_t> (start + i)]) * 0.5f;
    }
}

//==============================================================================
void Synth::render (float* const* channels, int numChannels, int numSamples)
{
    if (plant != nullptr)
        plant->acquire();

    std::stable_sort (pending.begin(), pending.end(),
                      [] (const PendingNote& a, const PendingNote& b)
                      {
                          return a.offset < b.offset;
                      });

    int position = 0;
    for (const auto& e : pending)
    {
        const int offset = std::min (std::max (e.offset, position), numSamples);
        if (offset > position)
        {
            renderSegment (channels, numChannels, position, offset - position);
            position = offset;
        }
        applyPending (e);
    }
    pending.clear();

    if (position < numSamples)
        renderSegment (channels, numChannels, position, numSamples - position);

    //------------------------------------------------------------------------
    // Master effects and output limiter.
    if (numChannels >= 2)
    {
        fx.process (channels[0], channels[1], numSamples);

        const float volume = play.macros.volume;
        for (int i = 0; i < numSamples; ++i)
        {
            // Runaway protection (design doc 5.6).
            channels[0][i] = softClip (channels[0][i] * volume) * 0.95f;
            channels[1][i] = softClip (channels[1][i] * volume) * 0.95f;
        }
    }
}

} // namespace gmosh
