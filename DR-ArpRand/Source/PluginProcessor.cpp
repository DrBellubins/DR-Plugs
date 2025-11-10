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

	// Arp rate
    parameterList.push_back(std::make_unique<juce::AudioParameterFloat>(
        "arpRate",                  // Parameter ID
        "Arp Rate",                 // Parameter name
        juce::NormalisableRange<float>(0.0f, 1.0f),
		0.4f                        // Default index: "1/4"
    ));

	// Octave range
	std::make_unique<juce::AudioParameterFloat>("octaveLower", "Octave Lower",
		juce::NormalisableRange<float>(-48.0f, 48.0f), -12.0f);

	std::make_unique<juce::AudioParameterFloat>("octaveHigher", "Octave Higher",
		juce::NormalisableRange<float>(-48.0f, 48.0f), 12.0f);

	// Free mode toggle
	parameterList.push_back(std::make_unique<juce::AudioParameterBool>(
		"isFreeMode",              // Parameter ID
		"Free Mode Toggle",        // Parameter name
		false                      // Default value
	));

	// Octaves toggle
	parameterList.push_back(std::make_unique<juce::AudioParameterBool>(
		"isOctaves",				// Parameter ID
		"Octaves Toggle",			// Parameter name
		false						// Default value
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

void AudioPluginAudioProcessor::handleArpStep(
	int64_t AbsoluteSamplePosition,
	int64_t SampleCursorPosition,
	double SamplesPerStep,
	juce::MidiBuffer& OutputMidiBuffer
)
{
	if (!heldNotes.empty())
	{
		// Turn off previous note
		if (noteIsOn && currentlyPlayingNote >= 0)
		{
			OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), (int)SampleCursorPosition);
			noteIsOn = false;
			currentlyPlayingNote = -1;
		}

		// Prevent the same note from being repeated
		while (currentlyPlayedNote == previousPlayedNote)
		{
			// Pick a random note to play
			std::random_device RandomDevice;
			std::mt19937 RandomGenerator(RandomDevice());
			std::uniform_int_distribution<size_t> Distribution(0, heldNotes.size() - 1);

			currentlyPlayedNote = heldNotes[Distribution(RandomGenerator)];
		}

		OutputMidiBuffer.addEvent(juce::MidiMessage::noteOn(1, currentlyPlayedNote, (juce::uint8)127), (int)SampleCursorPosition);
		currentlyPlayingNote = currentlyPlayedNote;
		noteOnSamplePosition = AbsoluteSamplePosition;
		noteIsOn = true;
		previousPlayedNote = currentlyPlayedNote;
	}
	else
	{
		// No notes held -- turn off any lingering note
		if (noteIsOn && currentlyPlayingNote >= 0)
		{
			OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), (int)SampleCursorPosition);
			noteIsOn = false;
			currentlyPlayingNote = -1;
		}
	}
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& AudioBuffer, juce::MidiBuffer& MidiMessages)
{
    if (BPM <= 0.0)
    {
        MidiMessages.clear();
        return;
    }

    juce::MidiBuffer OutputMidiBuffer;
    juce::AudioPlayHead::CurrentPositionInfo TransportInfo;

    if (getPlayHead() != nullptr)
    {
        getPlayHead()->getCurrentPosition(TransportInfo);
    }

    juce::AudioPlayHead* playHead = getPlayHead();

    if (playHead != nullptr)
    {
        juce::AudioPlayHead::CurrentPositionInfo positionInfo;

        if (playHead->getCurrentPosition(positionInfo))
        {
            if (positionInfo.bpm > 0.0)
            {
                BPM = positionInfo.bpm;
            }
        }
    }

    isPlaying = TransportInfo.isPlaying;

    if (!isPlaying && wasPlaying)
    {
        for (int HeldNote : heldNotes)
        {
            OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, HeldNote), 0);
        }

        if (noteIsOn && currentlyPlayingNote >= 0)
        {
            OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), 0);
        }

        noteIsOn = false;
        currentlyPlayingNote = -1;
    }

    float arpRate = parameters.getRawParameterValue("arpRate")->load();
    bool isFreeMode = parameters.getRawParameterValue("isFreeMode")->load() > 0.5f;

    double samplesPerStep = 0.0;

    if (isFreeMode)
    {
        float minFraction = 0.03125f;
        float maxFraction = 1.0f;
        float fraction = maxFraction * std::pow(minFraction / maxFraction, arpRate);
        samplesPerStep = (60.0 / BPM) * getSampleRate() * fraction;
    }
    else
    {
        float snappedArpRate = std::round(arpRate * 5.0f) / 5.0f;
        int index = static_cast<int>(snappedArpRate * 5.0f);
        index = juce::jlimit(0, 5, index);

        static constexpr float BeatFractionValues[] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f };
        float beatFraction = BeatFractionValues[index];

        samplesPerStep = (60.0 / BPM) * getSampleRate() * beatFraction;
    }

    updateHeldNotes(MidiMessages);

    const int64_t SongPositionSamples = TransportInfo.timeInSamples;
    const int blockNumSamples = AudioBuffer.getNumSamples();

    // Detect transport jump/start/loop. Arm to step on next held note, but be robust
    bool transportJumped = (
        lastSongPositionSamples < 0
        || SongPositionSamples != lastSongPositionSamples + lastBlockNumSamples
        || (!wasPlaying && isPlaying)
    );

    if (transportJumped)
    {
        waitingForFirstNote = true;
        pendingInitialStep = true; // will be handled below
    }

    // Strongest DAW-robustness: handle these cases:
    //  - If a NoteOn is present, and we were waiting for first, fire step and clear both flags
    //  - If heldNotes is populated and we are waiting, but grid-aligned NoteOn is not in buffer
    //          (for DAWs that only send note ons at block start), fire step at sample 0, and clear both flags

    bool noteOnFoundInBlock = false;
    int noteOnSamplePosition = 0;
    for (const auto& midiEvent : MidiMessages)
    {
        const juce::MidiMessage& midiMessage = midiEvent.getMessage();
        if (midiMessage.isNoteOn())
        {
            noteOnFoundInBlock = true;
            noteOnSamplePosition = midiEvent.samplePosition;
            break;
        }
    }

    if (waitingForFirstNote)
    {
        if (noteOnFoundInBlock)
        {
            handleArpStep(
                SongPositionSamples + noteOnSamplePosition,
                noteOnSamplePosition,
                samplesPerStep,
                OutputMidiBuffer
            );
            waitingForFirstNote = false;
            pendingInitialStep = false;
        }
        else if (!heldNotes.empty() && pendingInitialStep)
        {
            // No explicit NoteOn event, but held notes present (for DAWs that do not send note-ons on loop)
            handleArpStep(
                SongPositionSamples,
                0,
                samplesPerStep,
                OutputMidiBuffer
            );
            waitingForFirstNote = false;
            pendingInitialStep = false;
        }
        // Otherwise, continue waiting for incoming MIDI in future blocks
    }

    lastSongPositionSamples = SongPositionSamples;
    lastBlockNumSamples = blockNumSamples;

    // Sub-block scheduling for arpeggiator steps and note-offs (regular)
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
                samplesPerStep,
                OutputMidiBuffer
            );

            nextQuarterNoteSample += (int64_t)samplesPerStep;
        }

        int64_t nextEventSample = juce::jmin(nextQuarterNoteSample, blockStartSample + blockNumSamples);
        sampleCursor = nextEventSample - blockStartSample;
    }

    if (heldNotes.empty() && noteIsOn && currentlyPlayingNote >= 0)
    {
        OutputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), 0);
        noteIsOn = false;
        currentlyPlayingNote = -1;
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
void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
	// Save all parameters as XML
	auto xmlState = parameters.copyState().createXml();
	copyXmlToBinary(*xmlState, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
	// Load parameters from XML
	std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

	if (xmlState != nullptr)
	{
		parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
	}
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
