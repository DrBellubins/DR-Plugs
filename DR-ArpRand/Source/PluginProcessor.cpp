#include "PluginProcessor.h"

#include <random>

#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
{
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need.
    juce::ignoreUnused (sampleRate, samplesPerBlock);

    cachedSamplesPerQuarterNote = (60.0 / BPM) * sampleRate; // Replace 120.0 with actual BPM
    samplesProcessed = 0;
    lastQuarterNoteIndex = -1;
}

void AudioPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}

void AudioPluginAudioProcessor::processBlock(
    juce::AudioBuffer<float>& AudioBuffer,
    juce::MidiBuffer& MidiMessages)
{
    juce::MidiBuffer OutputMidiBuffer;
    juce::AudioPlayHead::CurrentPositionInfo TransportInfo;

    if (getPlayHead() != nullptr)
        getPlayHead()->getCurrentPosition(TransportInfo);

    // Calculate quarter note timing
    juce::AudioPlayHead* playHead = getPlayHead();

    if (playHead != nullptr)
    {
        juce::AudioPlayHead::CurrentPositionInfo positionInfo;

        if (playHead->getCurrentPosition(positionInfo))
        {
            if (positionInfo.bpm > 0.0)
                BPM = positionInfo.bpm;
        }
    }

    double SamplesPerQuarterNote = (60.0 / BPM) * getSampleRate();

    // Track held notes (from incoming MidiMessages)
    updateHeldNotes(MidiMessages); // You implement this

    // For deterministic random, compute quarter note index
    int64_t SongPositionSamples = TransportInfo.timeInSamples;
    int QuarterNoteIndex = static_cast<int>(SongPositionSamples / SamplesPerQuarterNote);

    // Seed PRNG with QuarterNoteIndex
    std::mt19937 RandomGenerator(QuarterNoteIndex);

    // At the start of each quarter note, select a random note from held notes
    if (isNewQuarterNote(AudioBuffer.getNumSamples())) // You detect this with a counter
    {
        if (!heldNotes.empty())
        {
            std::uniform_int_distribution<size_t> Distribution(0, heldNotes.size() - 1);
            int SelectedNote = heldNotes[Distribution(RandomGenerator)];

            // Output note-on/off as needed
            OutputMidiBuffer.addEvent(juce::MidiMessage::noteOn(1, SelectedNote, (juce::uint8)127), 0);
            // Optionally schedule note-off later, or add note-off for previous note
        }
    }

    MidiMessages.swapWith(OutputMidiBuffer);
}

void AudioPluginAudioProcessor::updateHeldNotes(const juce::MidiBuffer& MidiMessages)
{
    // Scan MIDI events in buffer and update heldNotes/heldNotesSet
    for (const auto& midiEvent : MidiMessages)
    {
        const juce::MidiMessage& midiMessage = midiEvent.getMessage();

        if (midiMessage.isNoteOn())
        {
            int noteNumber = midiMessage.getNoteNumber();

            if (heldNotesSet.find(noteNumber) == heldNotesSet.end())
            {
                heldNotes.push_back(noteNumber);
                heldNotesSet.insert(noteNumber);
            }
        }
        else if (midiMessage.isNoteOff())
        {
            int noteNumber = midiMessage.getNoteNumber();

            heldNotesSet.erase(noteNumber);

            auto it = std::find(heldNotes.begin(), heldNotes.end(), noteNumber);

            if (it != heldNotes.end())
                heldNotes.erase(it);
        }
    }
}

bool AudioPluginAudioProcessor::isNewQuarterNote(int numSamples)
{
    // Call this at the start of each processBlock(), passing buffer.getNumSamples()
    int currentQuarterNoteIndex = static_cast<int>(samplesProcessed / cachedSamplesPerQuarterNote);

    bool isNew = (currentQuarterNoteIndex != lastQuarterNoteIndex);

    samplesProcessed += numSamples;
    lastQuarterNoteIndex = currentQuarterNoteIndex;

    return isNew;
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    juce::ignoreUnused (destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
