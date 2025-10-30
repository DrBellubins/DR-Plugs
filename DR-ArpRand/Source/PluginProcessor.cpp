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

void shuffleBarOrder(const std::vector<int>& heldNotes)
{
    currentBarOrder = heldNotes;
    std::random_device randomDevice;
    std::mt19937 randomGenerator(randomDevice());

    // Shuffle until the order is different from the previous bar
    int maxAttempts = 10;
    int attempts = 0;

    do
    {
        std::shuffle(currentBarOrder.begin(), currentBarOrder.end(), randomGenerator);
        attempts++;
    }
    while (attempts < maxAttempts && currentBarOrder == previousBarNotes);

    currentBarNotes.clear();
    barNoteIndex = 0;
}


void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& AudioBuffer, juce::MidiBuffer& MidiMessages)
{
    juce::MidiBuffer OutputMidiBuffer;
    juce::AudioPlayHead::CurrentPositionInfo TransportInfo;

    if (getPlayHead() != nullptr)
        getPlayHead()->getCurrentPosition(TransportInfo);

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

    isPlaying = TransportInfo.isPlaying; // Get from DAW

    if (!isPlaying && wasPlaying)
    {
        // Playback just stopped - send note-off for all notes
        for (int HeldNote : heldNotes)
            OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, HeldNote), 0);

        if (noteIsOn && currentlyPlayingNote >= 0)
            OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), 0);

        noteIsOn = false;
        currentlyPlayingNote = -1;
    }

    const double SamplesPerQuarterNote = (60.0 / BPM) * getSampleRate();
    updateHeldNotes(MidiMessages);

    const int64_t SongPositionSamples = TransportInfo.timeInSamples;
    const int blockNumSamples = AudioBuffer.getNumSamples();

    // Sub-block scheduling for arpeggiator steps and note-offs
    const double samplesPerQuarterNote = SamplesPerQuarterNote;
    const int64_t blockStartSample = SongPositionSamples;

    int64_t sampleCursor = 0;

    int64_t nextQuarterNoteSample = ((blockStartSample / (int64_t)samplesPerQuarterNote) + 1) * (int64_t)samplesPerQuarterNote;

    while (sampleCursor < blockNumSamples)
    {
        int64_t absoluteSample = blockStartSample + sampleCursor;

        // Is it time for a new arpeggiator step?
        if (absoluteSample >= nextQuarterNoteSample)
        {
            int QuarterNoteIndex = static_cast<int>(nextQuarterNoteSample / samplesPerQuarterNote);
            std::mt19937 RandomGenerator(QuarterNoteIndex);

            // Pick note only if notes are held
            if (!heldNotes.empty())
            {
                std::uniform_int_distribution<size_t> Distribution(0, heldNotes.size() - 1);
                int SelectedNote = heldNotes[Distribution(RandomGenerator)];

                // Turn off previous note first if necessary
                if (noteIsOn && currentlyPlayingNote != SelectedNote && currentlyPlayingNote >= 0)
                {
                    OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), sampleCursor);
                }

                // Turn on new note
                OutputMidiBuffer.addEvent(juce::MidiMessage::noteOn(1, SelectedNote, (juce::uint8)127), sampleCursor);

                currentlyPlayingNote = SelectedNote;
                noteOnSamplePosition = nextQuarterNoteSample;
                noteIsOn = true;
            }
            else
            {
                // No notes held, turn off previous note
                if (noteIsOn && currentlyPlayingNote >= 0)
                {
                    OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), sampleCursor);
                    noteIsOn = false;
                    currentlyPlayingNote = -1;
                    noteOnSamplePosition = -1;
                }
            }

            nextQuarterNoteSample += (int64_t)samplesPerQuarterNote;
        }

        // Schedule note-off for currently playing note after 1 quarter note
        if (noteIsOn && noteOnSamplePosition >= 0)
        {
            int64_t noteOffSample = noteOnSamplePosition + (int64_t)samplesPerQuarterNote;
            int64_t noteOffOffset = noteOffSample - blockStartSample;

            if (noteOffOffset >= sampleCursor && noteOffOffset < blockNumSamples)
            {
                OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), (int)noteOffOffset);
                noteIsOn = false;
                currentlyPlayingNote = -1;
                noteOnSamplePosition = -1;
            }
        }

        // Advance to next event (either next quarter note or end of block)
        int64_t nextEventSample = juce::jmin(nextQuarterNoteSample, blockStartSample + blockNumSamples);
        sampleCursor = nextEventSample - blockStartSample;
    }

    wasPlaying = isPlaying;

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
