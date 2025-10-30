#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class AudioPluginAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    using AudioProcessor::processBlock;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    double BPM = 120.0;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    bool isNewQuarterNote(int numSamples);
    void updateHeldNotes(const juce::MidiBuffer& MidiMessages);
    void shuffleBarOrder(const std::vector<int>& heldNotes);

    void handleArpStep(
        int64_t AbsoluteSamplePosition,
        int64_t SampleCursorPosition,
        int64_t NextQuarterNoteSamplePosition,
        double SamplesPerQuarterNote,
        juce::MidiBuffer& OutputMidiBuffer
    );

    //==============================================================================
    std::vector<int> heldNotes;       // Vector of held MIDI note numbers, sorted
    std::set<int> heldNotesSet;       // For quick lookup

    int64_t samplesProcessed = 0;            // Total samples processed
    int lastQuarterNoteIndex = -1;           // Last quarter note index
    double cachedSamplesPerQuarterNote = 0;  // Cache value from prepareToPlay

    int currentlyPlayingNote = -1;
    int64_t noteOnSamplePosition = -1;
    bool noteIsOn = false;
    bool isPlaying = false;
    bool wasPlaying = false; // Track last transport state

    // Algorithmic markov chain/random
    std::vector<int> previousBarNotes;            // Notes played last bar
    std::vector<int> currentBarNotes;             // Notes played this bar
    int barNoteIndex = 0;                         // Note index within current bar
    int notesPerBar = 4;                          // e.g. 4 for 4/4 quarter notes per bar
    std::vector<int> currentBarOrder;             // The shuffled order for this bar
    int lastPlayedNote = -1;                      // For immediate repeat prevention
    int lastBarIndex = -1;                        // Track bar start

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
