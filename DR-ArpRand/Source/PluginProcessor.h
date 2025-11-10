#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

//==============================================================================
class AudioPluginAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

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

	void handleArpStep(
		int64_t AbsoluteSamplePosition,
		int64_t SampleCursorPosition,
		double SamplesPerStep,
		juce::MidiBuffer& OutputMidiBuffer
	);

    //==============================================================================
    std::vector<int> heldNotes;       // Vector of held MIDI note numbers, sorted
    std::set<int> heldNotesSet;       // For quick lookup

    int64_t samplesProcessed = 0;            // Total samples processed
	int64_t lastSongPositionSamples = -1;
	int64_t lastBlockNumSamples = 0;
    int lastQuarterNoteIndex = -1;           // Last quarter note index
    double cachedSamplesPerQuarterNote = 0;  // Cache value from prepareToPlay

	bool waitingForFirstNote = false;
    int currentlyPlayingNote = -1;
    int64_t noteOnSamplePosition = -1;
    bool noteIsOn = false;
    bool isPlaying = false;
    bool wasPlaying = false; // Track last transport state

	int currentlyPlayedNote = -1;
	int previousPlayedNote = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessor)
};
