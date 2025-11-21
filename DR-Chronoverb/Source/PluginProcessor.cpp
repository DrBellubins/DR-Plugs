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
    // IMPORTANT!
    parameters.addParameterListener("delayTime", this);
    parameters.addParameterListener("delayMode", this);

    parameters.addParameterListener("feedbackTime", this);
    parameters.addParameterListener("diffusionAmount", this);
    parameters.addParameterListener("diffusionSize", this);
    parameters.addParameterListener("diffusionQuality", this);
    parameters.addParameterListener("dryWetMix", this);

    // Filters
    parameters.addParameterListener("stereoSpread", this);
    parameters.addParameterListener("lowPassCutoff", this);
    parameters.addParameterListener("highPassCutoff", this);
    parameters.addParameterListener("hplpPrePost", this);

    // Ducking
    parameters.addParameterListener("duckAmount", this);
    parameters.addParameterListener("duckAttack", this);
    parameters.addParameterListener("duckRelease", this);

    // Set delay initial values
    float delayTime = parameters.getRawParameterValue("delayTime")->load();

    float feedbackTime = parameters.getRawParameterValue("feedbackTime")->load();
    float diffusionAmount = parameters.getRawParameterValue("diffusionAmount")->load();
    float diffusionSize = parameters.getRawParameterValue("diffusionSize")->load();
    float diffusionQuality = parameters.getRawParameterValue("diffusionQuality")->load();
    float dryWetMix = parameters.getRawParameterValue("dryWetMix")->load();

    // Filters
    float stereoSpread = parameters.getRawParameterValue("stereoSpread")->load();
    float lowPassCutoff = parameters.getRawParameterValue("lowPassCutoff")->load();
    float highPassCutoff = parameters.getRawParameterValue("highPassCutoff")->load();
    float hpLPPrePost = parameters.getRawParameterValue("hplpPrePost")->load();

    // Ducking
    float duckAmount = parameters.getRawParameterValue("duckAmount")->load();
    float duckAttack = parameters.getRawParameterValue("duckAttack")->load();
    float duckRelease = parameters.getRawParameterValue("duckRelease")->load();

    DelayReverb.SetDelayTime(delayTime);
    DelayReverb.SetFeedbackTime(feedbackTime);
    DelayReverb.SetDiffusionAmount(diffusionAmount);
    DelayReverb.SetDiffusionSize(diffusionSize);
    DelayReverb.SetDiffusionQuality(diffusionQuality);
    DelayReverb.SetDryWetMix(dryWetMix);

    DelayReverb.SetStereoSpread(stereoSpread);
    DelayReverb.SetLowpassCutoff(lowPassCutoff);
    DelayReverb.SetHighpassCutoff(highPassCutoff);
    DelayReverb.SetHPLPPrePost(hpLPPrePost);

    DelayReverb.SetDuckAmount(duckAmount);
    DelayReverb.SetDuckAttack(duckAttack);
    DelayReverb.SetDuckRelease(duckRelease);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterList;

    // Delay time
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "delayTime", "Delay Time",
        juce::NormalisableRange(0.0f, 1.0f), 0.3f)); // 300 ms default

    parameterList.push_back(std::make_unique<juce::AudioParameterChoice>(
        "delayMode", "Delay Mode",
        juce::StringArray{ "ms", "nrm", "trip", "dot" }, 0));

    // Feedback time
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "feedbackTime", "Feedback Time",
        juce::NormalisableRange(0.0f, 10.0f), 3.0f));

    // Diffusion amount
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "diffusionAmount", "Diffusion Amount",
        juce::NormalisableRange(0.0f, 1.0f), 0.0f));

    // Diffusion size
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "diffusionSize", "Diffusion Size",
        juce::NormalisableRange(0.0f, 1.0f), 0.0f));

    // Diffusion quality
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "diffusionQuality", "Diffusion Quality",
        juce::NormalisableRange(0.0f, 1.0f), 1.0f));

    // Dry/Wet mix
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "dryWetMix", "Dry/Wet mix",
        juce::NormalisableRange(0.0f, 1.0f), 0.5f));

    // ---- Filters ----

    // Spread (stereo reducing/widening)
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "stereoSpread", "Stereo Spread",
        juce::NormalisableRange(-1.0f, 1.0f), 0.0f));

    // Low pass cutoff
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "lowPassCutoff", "Low Pass Cutoff",
        juce::NormalisableRange(0.0f, 1.0f), 0.0f));

    // High pass cutoff
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "highPassCutoff", "High Pass Cutoff",
        juce::NormalisableRange(0.0f, 1.0f), 0.0f));

    parameterList.push_back(std::make_unique<juce::AudioParameterBool>(
        "hplpPrePost", "HP/LP Pre/Post", true));

    // ---- Ducking ----

    // Duck amount
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "duckAmount", "Duck Amount",
        juce::NormalisableRange(0.0f, 1.0f), 0.0f));

    // Duck attack
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "duckAttack", "Duck Attack",
        juce::NormalisableRange(0.0f, 1.0f), 0.3f)); // Default 300 ms

    // Duck release
    parameterList.push_back (std::make_unique<juce::AudioParameterFloat>(
        "duckRelease", "Duck Release",
        juce::NormalisableRange(0.0f, 1.0f), 0.3f)); // Default 300 ms

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

    keyboardSynth.PrepareToPlay(sampleRate);

    DelayReverb.PrepareToPlay(sampleRate, 1.0f);
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
    if (parameterID == "delayTime") DelayReverb.SetDelayTime(newValue);

    if (parameterID == "delayMode")
    {
        auto* mode = parameters.getParameter("delayMode");
        int modeIndex = (mode != nullptr) ? static_cast<int>(std::round(mode->convertFrom0to1(newValue))) : 0;

        DelayReverb.SetDelayMode(modeIndex);
    }

    if (parameterID == "feedbackTime") DelayReverb.SetFeedbackTime(newValue);
    if (parameterID == "diffusionAmount") DelayReverb.SetDiffusionAmount(newValue);
    if (parameterID == "diffusionSize") DelayReverb.SetDiffusionSize(newValue);
    if (parameterID == "diffusionQuality") DelayReverb.SetDiffusionQuality(newValue);
    if (parameterID == "dryWetMix") DelayReverb.SetDryWetMix(newValue);

    // Filters
    if (parameterID == "stereoSpread") DelayReverb.SetStereoSpread(newValue);
    if (parameterID == "lowPassCutoff") DelayReverb.SetLowpassCutoff(newValue);
    if (parameterID == "highPassCutoff") DelayReverb.SetHighpassCutoff(newValue);
    if (parameterID == "hplpPrePost") DelayReverb.SetHPLPPrePost(newValue);

    // Ducking
    if (parameterID == "duckAmount") DelayReverb.SetDuckAmount(newValue);
    if (parameterID == "duckAttack") DelayReverb.SetDuckAttack(newValue);
    if (parameterID == "duckRelease") DelayReverb.SetDuckRelease(newValue);

    #if DEBUG
    DBG("Changed: " << parameterID << " to " << newValue);
    #endif
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

    // Computer Keyboard Square Synth
    keyboardSynth.Process(buffer);

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
