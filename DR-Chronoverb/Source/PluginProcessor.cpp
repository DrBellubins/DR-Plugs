#include "PluginProcessor.h"
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

    // Add parameters here

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

void AudioPluginAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need.
    juce::ignoreUnused (sampleRate, samplesPerBlock);

    // Temporary - Needs to be delay range of the knob.
    DelayReverb.PrepareToPlay(sampleRate, 10.0f);

    // Temporary
    // Only set parameters AFTER PrepareToPlay
    DelayReverb.SetDelayTime(1.0f);           // 300 ms base delay
    DelayReverb.SetDiffusionAmount(0.0f);     // 70% reverb
    DelayReverb.SetDiffusionSize(0.6f);       // Medium-large space
    DelayReverb.SetDiffusionQuality(0.9f);    // Lush reverb
    DelayReverb.SetFeedback(0.5f);            // 50% feedback
    DelayReverb.SetWetDryMix(0.5f);           // 40% wet
}

void AudioPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
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

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // --------- Start: Square wave test generator (440 Hz, on 1s, off 3s) ---------
    const double squareTestFrequency = 440.0;
    const double squareTestSampleRate = getSampleRate();
    const int squareTestNumSamples = buffer.getNumSamples();
    const int squareTestOnSamples = static_cast<int>(squareTestSampleRate * 1.0);    // 1 sec ON
    const int squareTestOffSamples = static_cast<int>(squareTestSampleRate * 3.0);   // 3 sec OFF
    const double squareTestPhasePerSample = squareTestFrequency / squareTestSampleRate;

    for (int SampleIndex = 0; SampleIndex < squareTestNumSamples; ++SampleIndex)
    {
        // Square ON for X samples, then OFF for Y samples, repeat
        if (squareTestWaveOn)
        {
            // Write to ALL channels for audibility
            for (int Channel = 0; Channel < buffer.getNumChannels(); ++Channel)
            {
                float* channelData = buffer.getWritePointer(Channel);

                // Simple 0.5/-0.5 square (gentle on ears)
                if (squareTestPhase < 0.5)
                    channelData[SampleIndex] = 0.5f;
                else
                    channelData[SampleIndex] = -0.5f;
            }
        }

        // Silence in OFF section already handled by buffer initialization.

        // Advance phase
        squareTestPhase += squareTestPhasePerSample;

        if (squareTestPhase >= 1.0)
            squareTestPhase -= 1.0;

        // Advance and handle on/off switching
        squareTestSampleCounter++;

        if (squareTestWaveOn && squareTestSampleCounter >= squareTestOnSamples)
        {
            squareTestWaveOn = false;
            squareTestSampleCounter = 0;
        }
        else if (!squareTestWaveOn && squareTestSampleCounter >= squareTestOffSamples)
        {
            squareTestWaveOn = true;
            squareTestSampleCounter = 0;
            squareTestPhase = 0.0; // Optional: reset phase at re-trigger
        }
    }

    // Process reverb
    DelayReverb.ProcessBlock(buffer);
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
        parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
