#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Genetic/Curves.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "GENOMOSH_PARAMS", createParameterLayout())
{
    // A fixed initial seed keeps a fresh instance reproducible while still
    // being a real random genome rather than a hand written preset.
    seedIdentifier = gmosh::Hash::fromString("genomosh-initial-seed");
    syncGenerationParams();

    gmosh::Rng randomGenerator(seedIdentifier);
    gmosh::randomizeGenome(seed, randomGenerator, mutation.atonality);

    parameters.addParameterListener("atonality", this);

    rebuildPlant();
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    parameters.removeParameterListener("atonality", this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> list;

    auto addFloat = [&list](const juce::String& parameterId, const juce::String& parameterLabel,
                            float lowerValue, float upperValue, float defaultValue, float interval = 0.0f)
    {
        list.push_back(std::make_unique<juce::AudioParameterFloat>(
            parameterId, parameterLabel, juce::NormalisableRange<float>(lowerValue, upperValue, interval),
            defaultValue));
    };

    // Macro / performance parameters (design doc 4.1). The genome itself is
    // patch state, not a list of automatable parameters.
    addFloat("volume", "Volume", 0.0f, 1.0f, 0.75f);
    addFloat("velSens", "Vel Sens", 0.0f, 1.0f, 0.60f);
    addFloat("tuning", "Tuning", -100.0f, 100.0f, 0.0f);
    // 5.5, generation facing: a ceiling on how far the pitch capable genes may
    // go. 0 pins every one of them to no effect, 1 leaves the full range free.
    addFloat("atonality", "Atonality", 0.0f, 1.0f, 0.50f);
    addFloat("effect", "Effect", 0.0f, 1.0f, 0.60f);
    addFloat("wheel", "Mod Wheel", 0.0f, 1.0f, 1.00f);
    addFloat("wheelScale", "Wheel Scale", 0.0f, 1.0f, 0.85f);

    // The twelve branch lengths are part of the patch and are persisted, and
    // are automatable so a host can grow a branch over time.
    for (int branchIndex = 0; branchIndex < gmosh::kNumBranches; ++branchIndex)
    {
        addFloat(branchParamId(branchIndex), "Branch " + juce::String(branchIndex + 1), 0.0f, 1.0f,
                 kDefaultBranchLength);
    }

    return { list.begin(), list.end() };
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const { return JucePlugin_Name; }

bool AudioPluginAudioProcessor::acceptsMidi() const { return true; }
bool AudioPluginAudioProcessor::producesMidi() const { return false; }
bool AudioPluginAudioProcessor::isMidiEffect() const { return false; }

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return juce::jlimit(0.0, 12.0, synth.tailSeconds());
}

//==============================================================================
int AudioPluginAudioProcessor::getNumPrograms() { return 1; }
int AudioPluginAudioProcessor::getCurrentProgram() { return 0; }

void AudioPluginAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }

const juce::String AudioPluginAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.prepare(sampleRate, samplesPerBlock);
    synth.attachPlant(&handoff);
}

void AudioPluginAudioProcessor::releaseResources() { synth.releaseResources(); }

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
// Generation facing parameters: they are read once, when the plant is built,
// and never while a note sounds.
void AudioPluginAudioProcessor::syncGenerationParams()
{
    if (auto* atonalityParameter = parameters.getRawParameterValue("atonality"))
        mutation.atonality = gmosh::clamp01(atonalityParameter->load());
}

void AudioPluginAudioProcessor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID != "atonality")
        return;

    mutation.atonality = gmosh::clamp01(newValue);
    rebuildPlant();
}

//==============================================================================
void AudioPluginAudioProcessor::rebuildPlant()
{
    gmosh::PlantSnapshot& newSnapshot = handoff.beginWrite();

    gmosh::buildPlant(newSnapshot, seedIdentifier, seed, mutation);

    lastPlant = newSnapshot;
    handoff.publish();
}

void AudioPluginAudioProcessor::setSeedGene(std::size_t geneIndex, float normalisedValue)
{
    if (geneIndex >= gmosh::kNumGenes)
        return;

    seed[geneIndex] = gmosh::clamp01(normalisedValue);
    rebuildPlant();
}

void AudioPluginAudioProcessor::setSeedGenome(const gmosh::Genome& newGenome)
{
    seed = newGenome;
    gmosh::applyCouplings(seed);
    rebuildPlant();
}

void AudioPluginAudioProcessor::newSeed()
{
    // Deterministic seed chain: the same sequence of "new seed" presses gives
    // the same plants on every machine (design doc 10, determinism).
    ++seedIdentifier;
    seedIdentifier = gmosh::Hash::mix(seedIdentifier);

    syncGenerationParams();
    gmosh::Rng randomGenerator(seedIdentifier);
    gmosh::randomizeGenome(seed, randomGenerator, mutation.atonality);
    resetBranchLengths();
    rebuildPlant();
}

void AudioPluginAudioProcessor::resetBranchLengths()
{
    for (int branchIndex = 0; branchIndex < gmosh::kNumBranches; ++branchIndex)
    {
        if (auto* branchParameter = parameters.getParameter(branchParamId(branchIndex)))
            branchParameter->setValueNotifyingHost(gmosh::clamp01(kDefaultBranchLength));
    }
}

void AudioPluginAudioProcessor::plantBranch(int branchIndex, float distance)
{
    if (branchIndex < 0 || branchIndex >= gmosh::kNumBranches)
        return;

    gmosh::BranchTraits traits;

    const gmosh::Genome grownGenome =
        gmosh::resolveBranchGenome(lastPlant, branchIndex, gmosh::clamp01(distance), traits);

    // Planting folds the planted branch into the seed, so the growth of the old
    // plant goes with it (5.6.1: "branch lengths reset or decay").
    resetBranchLengths();
    setSeedGenome(grownGenome);
}

//==============================================================================
void AudioPluginAudioProcessor::playNote(int midiNote, float velocity)
{
    const juce::SpinLock::ScopedLockType scopedLock(uiMidiLock);
    uiMidi.addEvent(juce::MidiMessage::noteOn(1, midiNote, juce::jlimit(0.05f, 1.0f, velocity)), 0);
}

void AudioPluginAudioProcessor::stopNote(int midiNote)
{
    const juce::SpinLock::ScopedLockType scopedLock(uiMidiLock);
    uiMidi.addEvent(juce::MidiMessage::noteOff(1, midiNote), 0);
}

//==============================================================================
void AudioPluginAudioProcessor::handleMidiController(const juce::MidiMessage& message)
{
    if (message.getControllerNumber() == 1)
        midiWheel.store(message.getControllerValue() / 127.0f);
}

void AudioPluginAudioProcessor::fillPlayState(gmosh::PlayState& playState)
{
    auto raw = [this](const char* parameterId)
    { return parameters.getRawParameterValue(parameterId)->load(); };

    playState.macros.volume = raw("volume") * 1.15f;
    playState.macros.velSens = raw("velSens");
    playState.macros.tuningCents = raw("tuning");
    playState.macros.effect = raw("effect");
    playState.macros.wheelScale = raw("wheelScale");

    const float hostWheel = raw("wheel");
    const float midiWheelValue = midiWheel.load();
    playState.macros.wheel = midiWheelValue >= 0.0f ? midiWheelValue : hostWheel;

    liveWheel.store(playState.macros.wheel);

    for (int branchIndex = 0; branchIndex < gmosh::kNumBranches; ++branchIndex)
    {
        const juce::String branchId = branchParamId(branchIndex);
        playState.branchLength[static_cast<std::size_t>(branchIndex)] = raw(branchId.toRawUTF8());
    }
}

//==============================================================================
void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int channelIndex = totalNumInputChannels; channelIndex < totalNumOutputChannels; ++channelIndex)
        buffer.clear(channelIndex, 0, buffer.getNumSamples());

    // The synth mixes into the buffer, so the audio channels start empty.
    for (int channelIndex = 0; channelIndex < totalNumOutputChannels; ++channelIndex)
        buffer.clear(channelIndex, 0, buffer.getNumSamples());

    {
        const juce::SpinLock::ScopedLockType scopedLock(uiMidiLock);

        if (!uiMidi.isEmpty())
        {
            midiMessages.addEvents(uiMidi, 0, -1, 0);
            uiMidi.clear();
        }
    }

    gmosh::PlayState state;
    fillPlayState(state);

    synth.setPlayState(state);

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const int sampleOffset = metadata.samplePosition;

        if (message.isNoteOn())
            synth.addNoteOn(message.getNoteNumber(), message.getVelocity(), sampleOffset);
        else if (message.isNoteOff())
            synth.addNoteOff(message.getNoteNumber(), sampleOffset);
        else if (message.isController())
            handleMidiController(message);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            synth.allNotesOff();
    }

    synth.render(buffer.getArrayOfWritePointers(), totalNumOutputChannels, buffer.getNumSamples());
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor(*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto stateXml = std::make_unique<juce::XmlElement>("GenoMoshState");
    stateXml->setAttribute("version", 1);
    stateXml->setAttribute("mutationVersion", 1);

    stateXml->addChildElement(parameters.copyState().createXml().release());

    auto seedXml = std::make_unique<juce::XmlElement>("SEED");
    seedXml->setAttribute("seedId", juce::String(static_cast<juce::int64>(seedIdentifier)));

    for (std::size_t geneIndex = 0; geneIndex < gmosh::kNumGenes; ++geneIndex)
        seedXml->setAttribute(gmosh::kGenes[geneIndex].id, static_cast<double>(seed[geneIndex]));

    stateXml->addChildElement(seedXml.release());
    copyXmlToBinary(*stateXml, destData);
}

void AudioPluginAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml == nullptr)
        return;

    if (auto* parametersXml = xml->getChildByName("GENOMOSH_PARAMS"))
        parameters.replaceState(juce::ValueTree::fromXml(*parametersXml));

    if (auto* seedXml = xml->getChildByName("SEED"))
    {
        seedIdentifier = static_cast<uint64_t>(seedXml->getStringAttribute("seedId").getLargeIntValue());

        for (std::size_t geneIndex = 0; geneIndex < gmosh::kNumGenes; ++geneIndex)
        {
            const juce::String geneId = gmosh::kGenes[geneIndex].id;

            if (seedXml->hasAttribute(geneId))
                seed[geneIndex] =
                    gmosh::clamp01(static_cast<float>(seedXml->getDoubleAttribute(geneId, seed[geneIndex])));
        }
    }

    // The atonality ceiling lives in the parameters, so it has to be re read
    // before the plant is rebuilt from a recalled seed.
    syncGenerationParams();
    rebuildPlant();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AudioPluginAudioProcessor(); }
