#pragma once

#include "MasterFx.h"
#include "Voice.h"
#include "../Genetic/PlantEngine.h"

#include <atomic>
#include <vector>

namespace gmosh
//==============================================================================
// Voice pool and note -> branch mapping (design doc sections 5.3 and 7).
//
// Deliberately free of JUCE types: it takes raw channel pointers and note
// events, so the same engine can be driven from a command line render harness
// (design doc 13).
//==============================================================================
{
struct PlayState
{
    MacroValues macros;
    std::array<float, kNumBranches> branchLength{};
};

class Synth
{
  public:
    void prepare(double sampleRate, int maxBlockSamples);
    void releaseResources();

    void attachPlant(PlantHandoff* newPlant) { plant = newPlant; }

    /// Called from the audio thread at the start of every block.
    void setPlayState(const PlayState& newPlayState);

    /// Sample accurate note events, collected during the block's MIDI scan and
    /// applied inside render().
    void addNoteOn(int midiNote, float velocity, int sampleOffset);
    void addNoteOff(int midiNote, int sampleOffset);
    void allNotesOff();

    void render(float* const* channels, int numChannels, int numSamples);

    double tailSeconds() const { return masterFx.tailSeconds(); }
    int numActiveVoices() const;

    /// Most recently started branch index, for the UI plant view.
    int lastBranch() const { return lastPlayedBranch.load(); }

    /// Voice bookkeeping, for the offline harness and the debug UI.
    int numHeldNotes() const
    {
        int heldCount = 0;

        for (const auto& heldNote : heldNotes)
            if (heldNote.note >= 0)
                ++heldCount;

        return heldCount;
    }

  private:
    struct PendingNote
    {
        int note = 60;
        int sampleOffset = 0;
        float velocity = 1.0f;
        bool isNoteOn = false;
    };

    struct HeldNote
    {
        int note = -1;
        int voice = -1;
    };

    int allocateVoice();
    void startVoice(int voiceIndex, int midiNote, float velocity);
    void renderSegment(float* const* channels, int numChannels, int start, int numSamples);
    void applyPendingNote(const PendingNote& event);

    static constexpr int kMaxVoices = 16;

    std::vector<Voice> voices;
    MasterFx masterFx;
    PlantHandoff* plant = nullptr;
    PlayState playState;
    std::vector<PendingNote> pendingNotes;
    std::vector<HeldNote> heldNotes;
    std::vector<float> scratchL;
    std::vector<float> scratchR;
    double sampleRate = 44100.0;
    std::atomic<int> lastPlayedBranch{ -1 };
};

} // namespace gmosh
