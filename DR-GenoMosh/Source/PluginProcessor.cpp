#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Genetic/Curves.h"

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "GENOMOSH_PARAMS", createParameterLayout())
{
    // A fixed initial seed keeps a fresh instance reproducible while still
    // being a real random genome rather than a hand written preset.
    seedIdentifier = gmosh::hash::fromString ("genomosh-initial-seed");
    syncGenerationParams();

    gmosh::Rng rng (seedIdentifier);
    gmosh::randomizeGenome (seed, rng, mutation.atonality);

    parameters.addParameterListener ("atonality", this);

    rebuildPlant();
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    parameters.removeParameterListener ("atonality", this);
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> list;

    auto addFloat = [&list] (const juce::String& id, const juce::String& label,
                             float lo, float hi, float def, float interval = 0.0f)
    {
        list.push_back (std::make_unique<juce::AudioParameterFloat> (
            id, label, juce::NormalisableRange<float> (lo, hi, interval), def));
    };

    // Macro / performance parameters (design doc 4.1). The genome itself is
    // patch state, not a list of automatable parameters.
    addFloat ("volume",     "Volume",        0.0f, 1.0f,   0.75f);
    addFloat ("velSens",    "Vel Sens",      0.0f, 1.0f,   0.60f);
    addFloat ("tuning",     "Tuning",      -100.0f, 100.0f, 0.0f);
    // 5.5, generation facing: a ceiling on how far the pitch capable genes may
    // go. 0 pins every one of them to no effect, 1 leaves the full range free.
    addFloat ("atonality",  "Atonality",     0.0f, 1.0f,   0.50f);
    addFloat ("effect",     "Effect",        0.0f, 1.0f,   0.60f);
    addFloat ("wheel",      "Mod Wheel",     0.0f, 1.0f,   1.00f);
    addFloat ("wheelScale", "Wheel Scale",   0.0f, 1.0f,   0.85f);

    // The twelve branch lengths are part of the patch and are persisted, and
    // are automatable so a host can grow a branch over time.
    for (int b = 0; b < gmosh::kNumBranches; ++b)
        addFloat (branchParamId (b), "Branch " + juce::String (b + 1),
                  0.0f, 1.0f, kDefaultBranchLength);

    return { list.begin(), list.end() };
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const { return true; }
bool AudioPluginAudioProcessor::producesMidi() const { return false; }
bool AudioPluginAudioProcessor::isMidiEffect() const { return false; }

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return juce::jlimit (0.0, 12.0, synth.tailSeconds());
}

//==============================================================================
int AudioPluginAudioProcessor::getNumPrograms() { return 1; }
int AudioPluginAudioProcessor::getCurrentProgram() { return 0; }
void AudioPluginAudioProcessor::setCurrentProgram (int index) { juce::ignoreUnused (index); }
const juce::String AudioPluginAudioProcessor::getProgramName (int index) { juce::ignoreUnused (index); return {}; }
void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
    synth.attachPlant (&handoff);
}

void AudioPluginAudioProcessor::releaseResources()
{
    synth.releaseResources();
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
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
    if (auto* v = parameters.getRawParameterValue ("atonality"))
        mutation.atonality = gmosh::clamp01 (v->load());
}

void AudioPluginAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (parameterID != "atonality")
        return;

    mutation.atonality = gmosh::clamp01 (newValue);
    rebuildPlant();
}

//==============================================================================
void AudioPluginAudioProcessor::rebuildPlant()
{
    auto& target = handoff.beginWrite();
    gmosh::buildPlant (target, seedIdentifier, seed, mutation);
    lastPlant = target;
    handoff.publish();
}

void AudioPluginAudioProcessor::setSeedGene (std::size_t gene, float normalisedValue)
{
    if (gene >= gmosh::kNumGenes)
        return;

    seed[gene] = gmosh::clamp01 (normalisedValue);
    rebuildPlant();
}

void AudioPluginAudioProcessor::setSeedGenome (const gmosh::Genome& newGenome)
{
    seed = newGenome;
    gmosh::applyCouplings (seed);
    rebuildPlant();
}

void AudioPluginAudioProcessor::newSeed()
{
    // Deterministic seed chain: the same sequence of "new seed" presses gives
    // the same plants on every machine (design doc 10, determinism).
    ++seedIdentifier;
    seedIdentifier = gmosh::hash::mix (seedIdentifier);

    syncGenerationParams();
    gmosh::Rng rng (seedIdentifier);
    gmosh::randomizeGenome (seed, rng, mutation.atonality);
    resetBranchLengths();
    rebuildPlant();
}

void AudioPluginAudioProcessor::resetBranchLengths()
{
    for (int b = 0; b < gmosh::kNumBranches; ++b)
    {
        if (auto* p = parameters.getParameter (branchParamId (b)))
            p->setValueNotifyingHost (gmosh::clamp01 (kDefaultBranchLength));
    }
}

void AudioPluginAudioProcessor::plantBranch (int branch, float distance)
{
    if (branch < 0 || branch >= gmosh::kNumBranches)
        return;

    gmosh::BranchTraits traits;
    const auto grown = gmosh::resolveBranchGenome (lastPlant, branch,
                                                   gmosh::clamp01 (distance), traits);

    // Planting folds the planted branch into the seed, so the growth of the old
    // plant goes with it (5.6.1: "branch lengths reset or decay").
    resetBranchLengths();
    setSeedGenome (grown);
}

//==============================================================================
void AudioPluginAudioProcessor::playNote (int midiNote, float velocity)
{
    const juce::SpinLock::ScopedLockType sl (uiMidiLock);
    uiMidi.addEvent (juce::MidiMessage::noteOn (1, midiNote, juce::jlimit (0.05f, 1.0f, velocity)), 0);
}

void AudioPluginAudioProcessor::stopNote (int midiNote)
{
    const juce::SpinLock::ScopedLockType sl (uiMidiLock);
    uiMidi.addEvent (juce::MidiMessage::noteOff (1, midiNote), 0);
}

//==============================================================================
void AudioPluginAudioProcessor::handleMidiController (const juce::MidiMessage& message)
{
    if (message.getControllerNumber() == 1)
        midiWheel.store (message.getControllerValue() / 127.0f);
}

void AudioPluginAudioProcessor::fillPlayState (gmosh::PlayState& state)
{
    auto raw = [this] (const char* id) { return parameters.getRawParameterValue (id)->load(); };

    state.macros.volume     = raw ("volume") * 1.15f;
    state.macros.velSens    = raw ("velSens");
    state.macros.tuningCents= raw ("tuning");
    state.macros.effect     = raw ("effect");
    state.macros.wheelScale = raw ("wheelScale");

    const float hostWheel = raw ("wheel");
    const float midi = midiWheel.load();
    state.macros.wheel = midi >= 0.0f ? midi : hostWheel;

    liveWheel.store (state.macros.wheel);

    for (int b = 0; b < gmosh::kNumBranches; ++b)
    {
        const juce::String id = branchParamId (b);
        state.branchLength[static_cast<std::size_t> (b)] = raw (id.toRawUTF8());
    }
}

//==============================================================================
void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // The synth mixes into the buffer, so the audio channels start empty.
    for (int ch = 0; ch < totalNumOutputChannels; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    {
        const juce::SpinLock::ScopedLockType sl (uiMidiLock);
        if (! uiMidi.isEmpty())
        {
            midiMessages.addEvents (uiMidi, 0, -1, 0);
            uiMidi.clear();
        }
    }

    gmosh::PlayState state;
    fillPlayState (state);
    synth.setPlayState (state);

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const int offset = metadata.samplePosition;

        if (message.isNoteOn())
            synth.addNoteOn (message.getNoteNumber(), message.getVelocity(), offset);
        else if (message.isNoteOff())
            synth.addNoteOff (message.getNoteNumber(), offset);
        else if (message.isController())
            handleMidiController (message);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            synth.allNotesOff();
    }

    synth.render (buffer.getArrayOfWritePointers(), totalNumOutputChannels, buffer.getNumSamples());
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    return new AudioPluginAudioProcessorEditor (*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = std::make_unique<juce::XmlElement> ("GenoMoshState");
    state->setAttribute ("version", 1);
    state->setAttribute ("mutationVersion", 1);

    state->addChildElement (parameters.copyState().createXml().release());

    auto seedXml = std::make_unique<juce::XmlElement> ("SEED");
    seedXml->setAttribute ("seedId", juce::String (static_cast<juce::int64> (seedIdentifier)));
    for (std::size_t i = 0; i < gmosh::kNumGenes; ++i)
        seedXml->setAttribute (gmosh::kGenes[i].id, static_cast<double> (seed[i]));
    state->addChildElement (seedXml.release());

    copyXmlToBinary (*state, destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;

    if (auto* paramsXml = xml->getChildByName ("GENOMOSH_PARAMS"))
        parameters.replaceState (juce::ValueTree::fromXml (*paramsXml));

    if (auto* seedXml = xml->getChildByName ("SEED"))
    {
        seedIdentifier = static_cast<uint64_t> (seedXml->getStringAttribute ("seedId").getLargeIntValue());

        for (std::size_t i = 0; i < gmosh::kNumGenes; ++i)
        {
            const juce::String name = gmosh::kGenes[i].id;
            if (seedXml->hasAttribute (name))
                seed[i] = gmosh::clamp01 (static_cast<float> (seedXml->getDoubleAttribute (name, seed[i])));
        }
    }

    // The atonality ceiling lives in the parameters, so it has to be re read
    // before the plant is rebuilt from a recalled seed.
    syncGenerationParams();
    rebuildPlant();
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}
