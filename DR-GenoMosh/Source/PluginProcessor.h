#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/Synth.h"
#include "Genetic/PlantEngine.h"

//==============================================================================
/// GenoMosh: a genetic / organic synthesizer.
///
/// The message thread owns the seed genome and builds the 12 branch
/// directions; the audio thread only ever resolves a branch at the current
/// effective distance for a new note (design doc section 3).
class AudioPluginAudioProcessor : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener
{
  public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //==============================================================================
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Atonality is generation facing: changing it rebuilds the plant, it never
    // retunes a note that is already sounding.
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==============================================================================
    // Plant / seed access for the editor.
    const gmosh::Genome& seedGenome() const { return seed; }
    uint64_t seedId() const { return seedIdentifier; }
    /// The plant the message thread last built. The UI must not read the
    /// handoff slot: activeSlot is written by the audio thread.
    const gmosh::PlantSnapshot& lastPlantSnapshot() const { return lastPlant; }

    void setSeedGene(std::size_t geneIndex, float normalisedValue);
    void setSeedGenome(const gmosh::Genome& newGenome);

    /// Grow a brand new random seed (design doc 11 "randomize seed"). Also
    /// folds the twelve branch lengths back to their default: a new plant starts
    /// as a seed, not as a seed wearing the previous plant's growth.
    void newSeed();

    /// "Planting" a branch: that branch becomes the new seed (design doc 5.6.1).
    void plantBranch(int branchIndex, float distance);

    /// Branch lengths are patch state, so re seeding has to reset them
    /// explicitly (design doc 5.6.1: "branch lengths reset or decay").
    void resetBranchLengths();

    /// The value every branch length parameter starts at, and returns to.
    static constexpr float kDefaultBranchLength = 0.45f;

    /// For the UI on screen keyboard.
    void playNote(int midiNote, float velocity);
    void stopNote(int midiNote);

    /// Live values the UI shows but does not own.
    float currentWheel() const { return liveWheel.load(); }
    int lastPlayedBranch() const { return synth.lastBranch(); }

    /// Parameter ids, kept in one place so the UI and the audio code agree.
    static constexpr int kNumBranchParams = gmosh::kNumBranches;
    static juce::String branchParamId(int branchIndex) { return "branch" + juce::String(branchIndex + 1); }

  private:
    void rebuildPlant();
    void fillPlayState(gmosh::PlayState& playState);
    void handleMidiController(const juce::MidiMessage& message);
    void syncGenerationParams();

    gmosh::PlantHandoff handoff;
    gmosh::PlantSnapshot lastPlant; ///< message thread copy of the last built plant
    gmosh::Synth synth;
    gmosh::Genome seed{};
    uint64_t seedIdentifier = 0;
    gmosh::MutationParams mutation;

    std::atomic<float> midiWheel{ -1.0f };
    std::atomic<float> liveWheel{ 0.0f };

    juce::SpinLock uiMidiLock;
    juce::MidiBuffer uiMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};
