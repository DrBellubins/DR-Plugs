#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "PluginParameterRegistry.h"

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
    parameters(*this, nullptr, "PARAMS", PluginParameterRegistry::CreateLayout())
{
    PluginParameterRegistry::AddListeners(parameters, this);
    PluginParameterRegistry::ApplyAll(DelayReverb, parameters);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    PluginParameterRegistry::RemoveListeners(parameters, this);
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

    // Re-apply current parameter state before DSP prep.
    PluginParameterRegistry::ApplyAll(DelayReverb, parameters);

    //KeyboardSynth.PrepareToPlay(sampleRate);
    //ImpulseClick.PrepareToPlay(sampleRate);

    DelayReverb.PrepareToPlay(sampleRate);
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

void AudioPluginAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);
    PluginParameterRegistry::ApplyOneIfMatched(DelayReverb, parameters, parameterID);
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    // Square wave test
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // --- Host tempo update (once per block) ---
    if (auto* PlayHead = getPlayHead())
    {
        juce::Optional<juce::AudioPlayHead::PositionInfo> positionInfo = PlayHead->getPosition();
        auto bpm = positionInfo->getBpm();

        if (bpm.hasValue())
            DelayReverb.SetHostTempo(static_cast<float>(*bpm));
    }

    // Impulse response click
    //ImpulseClick.Process(buffer);

    // Computer Keyboard Square Synth
    //KeyboardSynth.Process(buffer);

    // Process reverb
    DelayReverb.ProcessBlock(buffer);

    // ---- Volume Clipper Section ----
    /*const float ClipperThreshold = 0.9f; // or 0.9f etc.
    const int NumChannels = buffer.getNumChannels();
    const int NumSamples = buffer.getNumSamples();

    for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
    {
        float* ChannelData = buffer.getWritePointer(ChannelIndex);

        for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
        {
            float InputSample = ChannelData[SampleIndex];

            // Simple hard-clip to [-ClipperThreshold, +ClipperThreshold]
            if (InputSample > ClipperThreshold)
                InputSample = ClipperThreshold;
            else if (InputSample < -ClipperThreshold)
                InputSample = -ClipperThreshold;

            ChannelData[SampleIndex] = InputSample;
        }
    }*/

    // --- Output safety sanitize: prevent DAW engine mute (NaN/Inf) ---
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer(ch);

            for (int i = 0; i < numSamples; ++i)
            {
                float x = data[i];

                if (!std::isfinite(x))
                {
                    debugInvalidSampleCount.fetch_add(1, std::memory_order_relaxed);
                    x = 0.0f;
                }

                const float ax = std::abs(x);
                float prev = debugMaxAbsSample.load(std::memory_order_relaxed);

                if (ax > prev)
                    debugMaxAbsSample.store(ax, std::memory_order_relaxed);

                // Prevent absurd spikes (can trigger host safety mute)
                x = juce::jlimit(-4.0f, 4.0f, x);

                // Kill denormals
                if (std::abs(x) < 1.0e-20f)
                    x = 0.0f;

                data[i] = x;
            }
        }
    }
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

    PluginParameterRegistry::ApplyAll(DelayReverb, parameters);
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
