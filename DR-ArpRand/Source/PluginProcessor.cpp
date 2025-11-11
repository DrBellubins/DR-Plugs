#include "PluginProcessor.h"
#include <random>
#include "PluginEditor.h"

// TODO: When going from vital to arprand, and then changing a value it crashes.
// TODO: Attach debugger to reaper and mess around till it crashes

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
	std::random_device rd;
	std::seed_seq seed { rd(), rd(), rd(), rd() };
	randomGenerator.seed (seed);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
	std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterList;

	// Arp rate
	parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
		"arpRate", "Arp Rate",
		juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));

	// <<< OCTAVE RANGE – MUST BE PUSHED >>>
	parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
		"octaveLower", "Octave Lower",
		juce::NormalisableRange<float>(-48.0f, 48.0f), -12.0f));

	parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
		"octaveHigher", "Octave Higher",
		juce::NormalisableRange<float>(-48.0f, 48.0f), 12.0f));
	// <<< END >>>

	// Free mode toggle
	parameterList.push_back (std::make_unique<juce::AudioParameterBool>(
		"isFreeMode", "Free Mode Toggle", false));

	// Octaves toggle
	parameterList.push_back (std::make_unique<juce::AudioParameterBool>(
		"isOctaves", "Octaves Toggle", false));

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

void AudioPluginAudioProcessor::handleArpStep (
    int64_t AbsoluteSamplePosition,
    int64_t SampleCursorPosition,
    double  SamplesPerStep,
    juce::MidiBuffer& OutputMidiBuffer)
{
    // --------------------------------------------------------------
    // 1. Turn off the note that is currently playing
    // --------------------------------------------------------------
    if (noteIsOn && currentlyPlayingNote >= 0)
    {
        OutputMidiBuffer.addEvent (
            juce::MidiMessage::noteOff (1, currentlyPlayingNote),
            static_cast<int> (SampleCursorPosition));

        noteIsOn = false;
        currentlyPlayingNote = -1;
    }

    // --------------------------------------------------------------
    // 2. Nothing held → stay silent
    // --------------------------------------------------------------
    if (heldNotes.empty())
        return;

    // --------------------------------------------------------------
    // 3. Pick a random held note (no repeats)
    // --------------------------------------------------------------
    std::uniform_int_distribution<size_t> noteDist (0, heldNotes.size() - 1);
    int selectedNote = heldNotes[noteDist (randomGenerator)];

    while (selectedNote == previousPlayedNote && heldNotes.size() > 1)
        selectedNote = heldNotes[noteDist (randomGenerator)];

    // --------------------------------------------------------------
    // 4. Octave transposition (safe parameter access)
    // --------------------------------------------------------------
    auto* octToggle = parameters.getRawParameterValue ("isOctaves");
    jassert (octToggle);
    const bool isOctavesEnabled = octToggle ? (octToggle->load() > 0.5f) : false;

    if (isOctavesEnabled)
    {
        auto* lowerParam = parameters.getRawParameterValue ("octaveLower");
        jassert (lowerParam);
        const float lower = lowerParam ? lowerParam->load() : -12.0f;

        auto* higherParam = parameters.getRawParameterValue ("octaveHigher");
        jassert (higherParam);
        const float higher = higherParam ? higherParam->load() : 12.0f;

        const int minOct = static_cast<int> (std::lround (lower  / 12.0f));
        const int maxOct = static_cast<int> (std::lround (higher / 12.0f));

        if (minOct < maxOct)
        {
            std::uniform_int_distribution<int> octDist (minOct, maxOct);
            const int octaveOffset = octDist (randomGenerator);
            selectedNote += octaveOffset * 12;
            selectedNote = juce::jlimit (0, 127, selectedNote);
        }
    }

    // --------------------------------------------------------------
    // 5. Send note-on
    // --------------------------------------------------------------
    OutputMidiBuffer.addEvent (
        juce::MidiMessage::noteOn (1,
                                   selectedNote,
                                   static_cast<juce::uint8> (127)),
        static_cast<int> (SampleCursorPosition));

    currentlyPlayingNote = selectedNote;
    noteOnSamplePosition = AbsoluteSamplePosition;
    noteIsOn             = true;
    previousPlayedNote   = selectedNote;
}

// TODO: On midi track repeat causes first note to play slightly delayed.
// TODO: Free mode automation does not update current time signature to play next note at newer relative signature.
void AudioPluginAudioProcessor::processBlock(
    juce::AudioBuffer<float>& audioBuffer,
    juce::MidiBuffer& midiMessages
)
{
    if (BPM <= 0.0)
    {
        midiMessages.clear();
        return;
    }

    juce::MidiBuffer outputMidiBuffer;
    juce::AudioPlayHead::CurrentPositionInfo transportInfo;

    // Query transport position (sample-accurate)
    if (getPlayHead() != nullptr)
    {
        getPlayHead()->getCurrentPosition(transportInfo);
    }

    // Update BPM from playhead, if available
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

    isPlaying = transportInfo.isPlaying;

    // On playback stop, immediately turn off any held or played notes
    if (!isPlaying && wasPlaying)
    {
        for (int heldNote : heldNotes)
        {
            outputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, heldNote), 0);
        }

        if (noteIsOn && currentlyPlayingNote >= 0)
        {
            outputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), 0);
        }

        noteIsOn = false;
        currentlyPlayingNote = -1;
        heldNotes.clear();
        heldNotesSet.clear();
        waitingForFirstNote = false;
        pendingInitialStep = false;
        previousPlayedNote = -1;
        currentlyPlayedNote = -1;
        previousHeldNoteCount = 0;
        lastSongPositionSamples = -1;
        lastBlockNumSamples = 0;
    }

    // ------- Calculate arpeggiator rate for this block -------
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

        static constexpr float beatFractionValues[] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f };
        float beatFraction = beatFractionValues[index];

        samplesPerStep = (60.0 / BPM) * getSampleRate() * beatFraction;
    }

    // Get time position and buffer info
    const int64_t songPositionSamples = transportInfo.timeInSamples;
    int blockNumSamples = audioBuffer.getNumSamples();
    int64_t blockStartSample = songPositionSamples;

    // Always update held notes BEFORE state logic
    updateHeldNotes(midiMessages);
    int currentHeldNoteCount = static_cast<int>(heldNotes.size());

    // -------- Detect start-of-block transport regions or playback events --------
    bool transportJumped = (
        lastSongPositionSamples < 0
        || songPositionSamples != lastSongPositionSamples + lastBlockNumSamples
        || (!wasPlaying && isPlaying)
    );

    bool arpStepTriggeredThisBlock = false;

    if (transportJumped)
    {
        stepPhase = 0.0;

        // Always flush note at jump (prevents hanging notes)
        if (noteIsOn && currentlyPlayingNote >= 0)
        {
            outputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), 0);
            noteIsOn = false;
            currentlyPlayingNote = -1;
        }

        // If any notes are ALREADY held at block start, trigger a step IMMEDIATELY at sample 0
        if (currentHeldNoteCount > 0)
        {
            handleArpStep(
                blockStartSample,
                0,
                samplesPerStep,
                outputMidiBuffer
            );
            waitingForFirstNote = false;
            arpStepTriggeredThisBlock = true;
            if (isFreeMode) stepPhase = 0.0;  // Reset phase after initial trigger
        }
        else
        {
            waitingForFirstNote = true;
        }
    }

    // --------- React immediately to new notes (notes going from 0->N held) ---------
    // Don't double-trigger if transport-trigger did it already
    if (!arpStepTriggeredThisBlock && (currentHeldNoteCount > 0 && previousHeldNoteCount == 0))
    {
        // Find the earliest note-on position in the buffer for accurate timing
        int earliestNoteOnPosition = blockNumSamples;
        for (const auto& midiEvent : midiMessages)
        {
            const juce::MidiMessage& midiMessage = midiEvent.getMessage();
            if (midiMessage.isNoteOn())
            {
                earliestNoteOnPosition = juce::jmin(earliestNoteOnPosition, midiEvent.samplePosition);
            }
        }

        if (earliestNoteOnPosition < blockNumSamples)
        {
            handleArpStep(
                blockStartSample + earliestNoteOnPosition,
                earliestNoteOnPosition,
                samplesPerStep,
                outputMidiBuffer
            );
            waitingForFirstNote = false;
            arpStepTriggeredThisBlock = true;
            if (isFreeMode) stepPhase = 0.0;  // Reset phase after re-start trigger
        }
    }
    // --------- On transport jump, wait for first note-on if necessary ---------
    else if (waitingForFirstNote && !arpStepTriggeredThisBlock)
    {
        bool anyNoteOn = false;
        int noteOnSamplePosition = 0;

        for (const auto& midiEvent : midiMessages)
        {
            const juce::MidiMessage& midiMessage = midiEvent.getMessage();

            if (midiMessage.isNoteOn())
            {
                anyNoteOn = true;
                noteOnSamplePosition = midiEvent.samplePosition;
                break;  // Use the first (earliest) note-on
            }
        }

        if (anyNoteOn)
        {
            handleArpStep(
                blockStartSample + noteOnSamplePosition,
                noteOnSamplePosition,
                samplesPerStep,
                outputMidiBuffer
            );

            waitingForFirstNote = false;
            arpStepTriggeredThisBlock = true;
            if (isFreeMode) stepPhase = 0.0;  // Reset phase after first trigger
        }
    }

    // --------- Step Scheduling: only if not already triggered in block ---------
    // (avoids two notes in one block right at start, especially after jumps)
    if (isFreeMode)
    {
        // Phase accumulator for smooth rate changes in free mode
        double delta = 1.0 / samplesPerStep;
        double phase = stepPhase;

        for (int sampleIndex = 0; sampleIndex < blockNumSamples; ++sampleIndex)
        {
            double oldPhase = phase;
            phase += delta;

            if (oldPhase < 1.0 && phase >= 1.0)
            {
                int triggerPosition = sampleIndex;
                bool isSameAsStart = (triggerPosition == 0);

                if (!isSameAsStart || !arpStepTriggeredThisBlock)
                {
                    handleArpStep(
                        blockStartSample + triggerPosition,
                        triggerPosition,
                        samplesPerStep,
                        outputMidiBuffer
                    );

                    arpStepTriggeredThisBlock = true;
                }

                phase -= std::floor(phase);
            }
        }

        stepPhase = phase;
    }
    else
    {
        // Original grid-based scheduling for synced/fractional mode
        int64_t sampleCursor = 0;
        int64_t nextStepSample = ((blockStartSample / static_cast<int64_t>(samplesPerStep)) + 1) * static_cast<int64_t>(samplesPerStep);

        while (sampleCursor < blockNumSamples)
        {
            int64_t absoluteSample = blockStartSample + sampleCursor;

            if (absoluteSample >= nextStepSample)
            {
                bool isSameSampleAsStart = (nextStepSample == blockStartSample);

                if (!isSameSampleAsStart || !arpStepTriggeredThisBlock)
                {
                    handleArpStep(
                        absoluteSample,
                        static_cast<int>(sampleCursor),
                        samplesPerStep,
                        outputMidiBuffer
                    );

                    arpStepTriggeredThisBlock = true;
                }

                nextStepSample += static_cast<int64_t>(samplesPerStep);
            }

            int64_t nextEventSample = juce::jmin(nextStepSample, blockStartSample + blockNumSamples);
            sampleCursor = nextEventSample - blockStartSample;
        }
    }

    // Always turn off current note if no keys are held
    if (heldNotes.empty() && noteIsOn && currentlyPlayingNote >= 0)
    {
        outputMidiBuffer.addEvent(juce::MidiMessage::noteOff(1, currentlyPlayingNote), 0);
        noteIsOn = false;
        currentlyPlayingNote = -1;
    }

    // Update state for next block
    wasPlaying = isPlaying;
    lastSongPositionSamples = songPositionSamples;
    lastBlockNumSamples = blockNumSamples;
    previousHeldNoteCount = currentHeldNoteCount;

    // Output Result
    midiMessages.swapWith(outputMidiBuffer);
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
