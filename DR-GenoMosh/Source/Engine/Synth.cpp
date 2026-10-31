#include "Synth.h"

#include <algorithm>

namespace gmosh
{
//==============================================================================
void Synth::prepare(double sampleRateHz, int maxBlockSamples)
{
    sampleRate = sampleRateHz;

    voices.clear();
    voices.resize(static_cast<std::size_t>(kMaxVoices));

    for (auto& voice : voices)
        voice.prepare(sampleRateHz, maxBlockSamples);

    masterFx.prepare(sampleRateHz, maxBlockSamples);

    // Reserve so the audio thread never allocates (design doc section 10).
    pendingNotes.clear();
    pendingNotes.reserve(128);
    heldNotes.clear();
    heldNotes.reserve(static_cast<std::size_t>(kMaxVoices));

    scratchL.assign(static_cast<std::size_t>(maxBlockSamples) + 1, 0.0f);
    scratchR.assign(static_cast<std::size_t>(maxBlockSamples) + 1, 0.0f);
}

void Synth::releaseResources()
{
    masterFx.reset();
    heldNotes.clear();
    pendingNotes.clear();
}

//==============================================================================
void Synth::setPlayState(const PlayState& newPlayState) { playState = newPlayState; }

//==============================================================================
void Synth::addNoteOn(int midiNote, float velocity, int sampleOffset)
{
    PendingNote event;
    event.note = midiNote;
    event.velocity = velocity;
    event.sampleOffset = sampleOffset;
    event.isNoteOn = true;
    pendingNotes.push_back(event);
}

void Synth::addNoteOff(int midiNote, int sampleOffset)
{
    PendingNote event;
    event.note = midiNote;
    event.sampleOffset = sampleOffset;
    event.isNoteOn = false;
    pendingNotes.push_back(event);
}

void Synth::allNotesOff()
{
    for (auto& voice : voices)
        voice.clearActive();

    heldNotes.clear();
    pendingNotes.clear();
    masterFx.reset();
}

//==============================================================================
int Synth::numActiveVoices() const
{
    int activeCount = 0;

    for (const auto& voice : voices)
        if (voice.isActive())
            ++activeCount;

    return activeCount;
}

//==============================================================================
int Synth::allocateVoice()
{
    for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
    {
        if (!voices[voiceIndex].isActive())
            return static_cast<int>(voiceIndex);
    }

    // Release aware stealing: take the quietest voice.
    int bestVoiceIndex = 0;
    float bestLevel = 1.0e9f;

    for (std::size_t voiceIndex = 0; voiceIndex < voices.size(); ++voiceIndex)
    {
        const float level = voices[voiceIndex].ampLevel();

        if (level < bestLevel)
        {
            bestLevel = level;
            bestVoiceIndex = static_cast<int>(voiceIndex);
        }
    }

    voices[static_cast<std::size_t>(bestVoiceIndex)].release();
    return bestVoiceIndex;
}

//==============================================================================
void Synth::startVoice(int voiceIndex, int midiNote, float velocity)
{
    if (plant == nullptr || voiceIndex < 0)
        return;

    const PlantSnapshot& snapshot = plant->current();

    const int branchIndex = branchForNote(midiNote);
    const float branchDistance =
        effectiveDistance(playState.branchLength[static_cast<std::size_t>(branchIndex)],
                          playState.macros.wheel, playState.macros.wheelScale);

    BranchTraits traits;
    const Genome genome = resolveBranchGenome(snapshot, branchIndex, branchDistance, traits);

    VoiceParams params;
    resolveVoiceParams(genome, traits, playState.macros, midiNote, velocity, params);

    const uint64_t voiceHash = Hash::combine(snapshot.seedId, static_cast<uint64_t>(branchIndex));
    voices[static_cast<std::size_t>(voiceIndex)].start(params, midiNote, voiceHash);

    lastPlayedBranch.store(branchIndex);

    // The tail follows the timbre of what was just played (design doc 8).
    masterFx.setParams(params.fxSize, params.fxLength, params.fxDamp, params.fxChorus, params.fxMix,
                       params.fxWidth);
}

//==============================================================================
void Synth::applyPendingNote(const PendingNote& pendingNote)
{
    // One slot per note, reused: a re trigger of a note that is already held
    // re articulates its own voice instead of adding a second tracking entry
    // (which used to grow heldNotes without bound while a branch was dragged).
    int slot = -1;

    for (std::size_t heldIndex = 0; heldIndex < heldNotes.size(); ++heldIndex)
    {
        if (heldNotes[heldIndex].note == pendingNote.note)
        {
            slot = static_cast<int>(heldIndex);
            break;
        }
    }

    if (pendingNote.isNoteOn)
    {
        int voiceIndex = slot >= 0 ? heldNotes[static_cast<std::size_t>(slot)].voice : -1;

        if (voiceIndex < 0 || !voices[static_cast<std::size_t>(voiceIndex)].isActive())
            voiceIndex = allocateVoice();

        startVoice(voiceIndex, pendingNote.note, pendingNote.velocity);

        if (slot >= 0)
            heldNotes[static_cast<std::size_t>(slot)].voice = voiceIndex;
        else
        {
            bool isTracked = false;

            for (auto& heldNote : heldNotes)
            {
                if (heldNote.note < 0)
                {
                    heldNote.note = pendingNote.note;
                    heldNote.voice = voiceIndex;
                    isTracked = true;
                    break;
                }
            }

            if (!isTracked)
                heldNotes.push_back({ pendingNote.note, voiceIndex });
        }
    }
    else if (slot >= 0)
    {
        auto& heldNote = heldNotes[static_cast<std::size_t>(slot)];

        if (heldNote.voice >= 0)
            voices[static_cast<std::size_t>(heldNote.voice)].release();

        heldNote.note = -1;
        heldNote.voice = -1;
    }
}

//==============================================================================
void Synth::renderSegment(float* const* channels, int numChannels, int start, int numSamples)
{
    if (numSamples <= 0)
        return;

    float* leftChannel = channels[0] + start;
    float* rightChannel = numChannels >= 2 ? channels[1] + start : scratchR.data() + start;

    if (numChannels < 2)
        leftChannel = scratchL.data() + start;

    for (auto& voice : voices)
    {
        if (voice.isActive())
            voice.render(leftChannel, rightChannel, numSamples);
    }

    if (numChannels < 2)
    {
        float* monoChannel = channels[0] + start;

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
            monoChannel[sampleIndex] += (scratchL[static_cast<std::size_t>(start + sampleIndex)]
                                         + scratchR[static_cast<std::size_t>(start + sampleIndex)])
                                        * 0.5f;
    }
}

//==============================================================================
void Synth::render(float* const* channels, int numChannels, int numSamples)
{
    if (plant != nullptr)
        plant->acquire();

    std::stable_sort(pendingNotes.begin(), pendingNotes.end(),
                     [](const PendingNote& first, const PendingNote& second)
                     { return first.sampleOffset < second.sampleOffset; });

    int position = 0;

    for (const auto& event : pendingNotes)
    {
        const int eventOffset = std::min(std::max(event.sampleOffset, position), numSamples);

        if (eventOffset > position)
        {
            renderSegment(channels, numChannels, position, eventOffset - position);
            position = eventOffset;
        }

        applyPendingNote(event);
    }

    pendingNotes.clear();

    if (position < numSamples)
        renderSegment(channels, numChannels, position, numSamples - position);

    //------------------------------------------------------------------------
    // Master effects and output limiter.
    if (numChannels >= 2)
    {
        masterFx.process(channels[0], channels[1], numSamples);

        const float volume = playState.macros.volume;

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            // Runaway protection (design doc 5.6).
            channels[0][sampleIndex] = softClip(channels[0][sampleIndex] * volume) * 0.95f;
            channels[1][sampleIndex] = softClip(channels[1][sampleIndex] * volume) * 0.95f;
        }
    }
}

} // namespace gmosh
