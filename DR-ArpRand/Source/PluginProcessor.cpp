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
                       ),
    parameters(*this, nullptr, "PARAMS", createParameterLayout())
{
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterList;

    parameterList.push_back(std::make_unique<juce::AudioParameterFloat>(
        "arpRate",                  // Parameter ID
        "Arp Rate",                 // Parameter name
        juce::NormalisableRange<float>(0.0f, 1.0f),
		0.5f                        // Default index: "1/8"
    ));

	parameterList.push_back(std::make_unique<juce::AudioParameterBool>(
		"isFreeRate",              // Parameter ID
		"Free Rate Toggle",        // Parameter name
		false                      // Default value
	));

    return { parameterList.begin(), parameterList.end() };
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

void AudioPluginAudioProcessor::shuffleBarOrder(const std::vector<int>& heldNotes)
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

void AudioPluginAudioProcessor::handleArpStep(
    int64_t AbsoluteSamplePosition,
    int64_t SampleCursorPosition,
    int64_t NextQuarterNoteSamplePosition,
    double SamplesPerQuarterNote,
    juce::MidiBuffer& OutputMidiBuffer
)
{
    // Calculate bar index
    int QuartersPerBar = notesPerBar;
    int64_t SamplesPerBar = SamplesPerQuarterNote * QuartersPerBar;
    int CurrentBarIndex = static_cast<int>(AbsoluteSamplePosition / SamplesPerBar);

    // If the bar changed or held notes changed, reshuffle
    bool BarChanged = (CurrentBarIndex != lastBarIndex);
    bool NotesChanged = (heldNotes.size() != currentBarOrder.size() ||
                         !std::equal(heldNotes.begin(), heldNotes.end(), currentBarOrder.begin()));

    if ((BarChanged || NotesChanged) && !heldNotes.empty())
    {
        shuffleBarOrder(heldNotes);
        lastBarIndex = CurrentBarIndex;
    }

    // Pick note if notes are held
    if (!heldNotes.empty())
    {
        int NoteIndex = barNoteIndex % currentBarOrder.size();
        int SelectedNote = currentBarOrder[NoteIndex];

        // Prevent immediate repeat
        if (SelectedNote == lastPlayedNote && currentBarOrder.size() > 1)
        {
            for (size_t Offset = 1; Offset < currentBarOrder.size(); ++Offset)
            {
                int TryNote = currentBarOrder[(NoteIndex + Offset) % currentBarOrder.size()];
                if (TryNote != lastPlayedNote)
                {
                    SelectedNote = TryNote;
                    break;
                }
            }
        }

        // Turn off previous note first if necessary
        if (noteIsOn && currentlyPlayingNote != SelectedNote && currentlyPlayingNote >= 0)
        {
            OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), (int)SampleCursorPosition);
        }

        // Turn on new note
        OutputMidiBuffer.addEvent(juce::MidiMessage::noteOn(1, SelectedNote, (juce::uint8)127), (int)SampleCursorPosition);

        currentlyPlayingNote = SelectedNote;
        noteOnSamplePosition = NextQuarterNoteSamplePosition;
        noteIsOn = true;

        currentBarNotes.push_back(SelectedNote);
        lastPlayedNote = SelectedNote;
        barNoteIndex++;
    }
    else
    {
        if (noteIsOn && currentlyPlayingNote >= 0)
        {
            OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), (int)SampleCursorPosition);
            noteIsOn = false;
            currentlyPlayingNote = -1;
            noteOnSamplePosition = -1;
        }
    }

    // End of bar: Save current bar notes as previousBarNotes
    if ((barNoteIndex % notesPerBar) == 0 && !currentBarNotes.empty())
    {
        previousBarNotes = currentBarNotes;
        currentBarNotes.clear();
    }
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& AudioBuffer, juce::MidiBuffer& MidiMessages)
{
    // We're stopped, clear midi and bypass processing.
    if (BPM <= 0.0)
    {
        MidiMessages.clear();
        return;
    }

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

	float arpRate = parameters.getRawParameterValue("arpRate")->load();
	bool isFreeRate = parameters.getRawParameterValue("isFreeRate")->load() > 0.5;

	int arpRateIndex = static_cast<int>(std::round(arpRate * 5.0f));
	arpRateIndex = juce::jlimit(0, 5, arpRateIndex);

	double rateMultiplier = 0.0;

	float minHzMult = 0.03125f;
	float maxHzMult = 1.0f;

	// Map index to actual timing
	// 0 = 1/1, 1 = 1/2, 2 = 1/4, 3 = 1/8, etc.
	const double rateMultipliers[] = { 1.0, 0.5, 0.25, 0.125, 0.0625, 0.03125 };

	if (isFreeRate)
	{
		double hzValue = arpRate * (maxHzMult - minHzMult) + minHzMult;
		rateMultiplier = hzValue;
	}
	else
	{
		rateMultiplier = rateMultipliers[arpRateIndex];
	}

    double samplesPerStep = (60.0 / BPM) * getSampleRate() * rateMultiplier;
    updateHeldNotes(MidiMessages);

    const int64_t SongPositionSamples = TransportInfo.timeInSamples;
    const int blockNumSamples = AudioBuffer.getNumSamples();

    // Sub-block scheduling for arpeggiator steps and note-offs
    const int64_t blockStartSample = SongPositionSamples;

    int64_t sampleCursor = 0;
    int64_t nextQuarterNoteSample = ((blockStartSample / (int64_t)samplesPerStep) + 1) * (int64_t)samplesPerStep;

    while (sampleCursor < blockNumSamples)
    {
        int64_t absoluteSample = blockStartSample + sampleCursor;

        if (absoluteSample >= nextQuarterNoteSample)
        {
            handleArpStep(
                absoluteSample,
                sampleCursor,
                nextQuarterNoteSample,
                samplesPerStep,
                OutputMidiBuffer
            );

            nextQuarterNoteSample += (int64_t)samplesPerStep;
        }

        // Advance sampleCursor to next event or end of block
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
