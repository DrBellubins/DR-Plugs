#include "PluginParameterRegistry.h"

#include "Filters/Chronoverb.h"

namespace
{
    using Entry = PluginParameterRegistry::Entry;

    Entry MakeFloat(const juce::String& parameterID,
                    const juce::String& parameterName,
                    const juce::NormalisableRange<float>& range,
                    float defaultValue,
                    std::function<void(Chronoverb&, float)> apply)
    {
        return Entry
        {
            parameterID,
            parameterName,
            [parameterID, parameterName, range, defaultValue]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterFloat>(
                    parameterID,
                    parameterName,
                    range,
                    defaultValue);
            },
            [parameterID, apply](Chronoverb& chronoverb,
                                 juce::AudioProcessorValueTreeState& apvts,
                                 const juce::String&)
            {
                if (auto* raw = apvts.getRawParameterValue(parameterID))
                    apply(chronoverb, raw->load());
            }
        };
    }

    Entry MakeChoice(const juce::String& parameterID,
                     const juce::String& parameterName,
                     const juce::StringArray& choices,
                     int defaultIndex,
                     std::function<void(Chronoverb&, int)> apply)
    {
        return Entry
        {
            parameterID,
            parameterName,
            [parameterID, parameterName, choices, defaultIndex]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterChoice>(
                    parameterID,
                    parameterName,
                    choices,
                    defaultIndex);
            },
            [parameterID, apply](Chronoverb& chronoverb,
                                 juce::AudioProcessorValueTreeState& apvts,
                                 const juce::String&)
            {
                if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(parameterID)))
                    apply(chronoverb, parameter->getIndex());
            }
        };
    }

    Entry MakeBool(const juce::String& parameterID,
                   const juce::String& parameterName,
                   bool defaultValue,
                   std::function<void(Chronoverb&, bool)> apply)
    {
        return Entry
        {
            parameterID,
            parameterName,
            [parameterID, parameterName, defaultValue]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterBool>(
                    parameterID,
                    parameterName,
                    defaultValue);
            },
            [parameterID, apply](Chronoverb& chronoverb,
                                 juce::AudioProcessorValueTreeState& apvts,
                                 const juce::String&)
            {
                if (auto* raw = apvts.getRawParameterValue(parameterID))
                    apply(chronoverb, raw->load() >= 0.5f);
            }
        };
    }

    const std::vector<Entry>& BuildEntries()
    {
        static const std::vector<Entry> entries =
        {
            // ---- Delay ----
            MakeFloat(
                "delayTime",
                "Delay Time",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.3f,
                [](Chronoverb& c, float v) { c.SetDelayTime(v); }),

            MakeChoice(
                "delayTimeMode",
                "Delay Time Mode",
                juce::StringArray{ "ms", "nrm", "trip", "dot" },
                0,
                [](Chronoverb& c, int v) { c.SetDelayMode(v); }),

            MakeFloat(
                "feedbackTime",
                "Feedback Time",
                juce::NormalisableRange<float>(0.0f, 10.0f),
                5.0f,
                [](Chronoverb& c, float v) { c.SetFeedbackTime(v); }),

            // ---- Diffusion ----
            MakeFloat(
                "diffusionAmount",
                "Diffusion Amount",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.0f,
                [](Chronoverb& c, float v) { c.SetDiffusionAmount(v); }),

            MakeFloat(
                "diffusionSize",
                "Diffusion Size",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.0f,
                [](Chronoverb& c, float v) { c.SetDiffusionSize(v); }),

            MakeChoice(
                "diffusionQuality",
                "Diffusion Quality",
                juce::StringArray{ "1", "2", "3", "4", "5", "6", "7", "8" },
                7,
                [](Chronoverb& c, int v) { c.SetDiffusionQuality(v + 1); }),

            // ---- Dry/Wet ----
            MakeFloat(
                "dryVolume",
                "Dry Volume",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                1.0f,
                [](Chronoverb& c, float v) { c.SetDryVolume(v); }),

            MakeFloat(
                "wetVolume",
                "Wet Volume",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                1.0f,
                [](Chronoverb& c, float v) { c.SetWetVolume(v); }),

            // ---- Filters ----
            MakeFloat(
                "stereoSpread",
                "Stereo Spread",
                juce::NormalisableRange<float>(-1.0f, 1.0f),
                0.0f,
                [](Chronoverb& c, float v) { c.SetStereoSpread(v); }),

            MakeFloat(
                "lowPassCutoff",
                "Low Pass Cutoff",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                1.0f,
                [](Chronoverb& c, float v) { c.SetLowpassCutoff(v); }),

            MakeFloat(
                "highPassCutoff",
                "High Pass Cutoff",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.0f,
                [](Chronoverb& c, float v) { c.SetHighpassCutoff(v); }),

            MakeBool(
                "hplpPrePost",
                "HP/LP Pre/Post",
                true,
                [](Chronoverb& c, bool v) { c.SetHPLPPrePost(v ? 1.0f : 0.0f); }),

            // ---- Ducking ----
            MakeFloat(
                "duckAmount",
                "Duck Amount",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.0f,
                [](Chronoverb& c, float v) { c.SetDuckAmount(v); }),

            MakeFloat(
                "duckAttack",
                "Duck Attack",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.3f,
                [](Chronoverb&, float)
                {
                    // Hook up later when Chronoverb exposes SetDuckAttack(...)
                }),

            MakeFloat(
                "duckRelease",
                "Duck Release",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.3f,
                [](Chronoverb&, float)
                {
                    // Hook up later when Chronoverb exposes SetDuckRelease(...)
                }),

            // ---- Pitch ----
            MakeFloat(
                "pitchRangeLower",
                "Pitch Shift Range Lower",
                juce::NormalisableRange<float>(-48.0f, 48.0f, 12.0f),
                -12.0f,
                [](Chronoverb& c, float v) { c.SetPitchRangeLower(v); }),

            MakeFloat(
                "pitchRangeUpper",
                "Pitch Shift Range Upper",
                juce::NormalisableRange<float>(-48.0f, 48.0f, 12.0f),
                12.0f,
                [](Chronoverb& c, float v) { c.SetPitchRangeUpper(v); }),

            MakeChoice(
                "pitchSequence",
                "Pitch Shift Sequence",
                juce::StringArray{ "Up", "Down", "Random", "Up-Down" },
                0,
                [](Chronoverb& c, int v) { c.SetPitchSequence(v); }),

            MakeBool(
                "pitchStereoEnabled",
                "Pitch Shift Stereo Enabled",
                false,
                [](Chronoverb& c, bool v) { c.SetPitchStereoEnabled(v ? 1.0f : 0.0f); }),

            MakeFloat(
                "pitchWetMix",
                "Pitch Shift Wet Mix",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.0f,
                [](Chronoverb& c, float v) { c.SetpitchWetMix(v); })
        };

        return entries;
    }
}

const std::vector<PluginParameterRegistry::Entry>& PluginParameterRegistry::GetEntries()
{
    return BuildEntries();
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginParameterRegistry::CreateLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterList;
    const auto& entries = GetEntries();

    parameterList.reserve(entries.size());

    for (const auto& entry : entries)
        parameterList.push_back(entry.createParameter());

    return { parameterList.begin(), parameterList.end() };
}

void PluginParameterRegistry::AddListeners(juce::AudioProcessorValueTreeState& apvts,
                                           juce::AudioProcessorValueTreeState::Listener* listener)
{
    for (const auto& entry : GetEntries())
        apvts.addParameterListener(entry.parameterID, listener);
}

void PluginParameterRegistry::RemoveListeners(juce::AudioProcessorValueTreeState& apvts,
                                              juce::AudioProcessorValueTreeState::Listener* listener)
{
    for (const auto& entry : GetEntries())
        apvts.removeParameterListener(entry.parameterID, listener);
}

void PluginParameterRegistry::ApplyAll(Chronoverb& chronoverb,
                                       juce::AudioProcessorValueTreeState& apvts)
{
    for (const auto& entry : GetEntries())
        entry.applyFromState(chronoverb, apvts, entry.parameterID);
}

bool PluginParameterRegistry::ApplyOneIfMatched(Chronoverb& chronoverb,
                                                juce::AudioProcessorValueTreeState& apvts,
                                                const juce::String& changedParameterID)
{
    for (const auto& entry : GetEntries())
    {
        if (entry.parameterID == changedParameterID)
        {
            entry.applyFromState(chronoverb, apvts, changedParameterID);
            return true;
        }
    }

    return false;
}

float PluginParameterRegistry::ReadFloat(juce::AudioProcessorValueTreeState& apvts,
                                         const juce::String& parameterID)
{
    if (auto* raw = apvts.getRawParameterValue(parameterID))
        return raw->load();

    jassertfalse;
    return 0.0f;
}

int PluginParameterRegistry::ReadChoiceIndex(juce::AudioProcessorValueTreeState& apvts,
                                             const juce::String& parameterID)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(parameterID)))
        return choice->getIndex();

    jassertfalse;
    return 0;
}

bool PluginParameterRegistry::ReadBool(juce::AudioProcessorValueTreeState& apvts,
                                       const juce::String& parameterID)
{
    if (auto* raw = apvts.getRawParameterValue(parameterID))
        return raw->load() >= 0.5f;

    jassertfalse;
    return false;
}