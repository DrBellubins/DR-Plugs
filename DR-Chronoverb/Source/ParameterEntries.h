#pragma once

#include "ParameterEntryTypes.h"
#include "Filters/Chronoverb.h"

namespace ParameterEntries
{
    inline void AddDistortionModuleEntries(std::vector<PluginParameterRegistry::Entry>& entries, int moduleIndex);

    inline const std::vector<PluginParameterRegistry::Entry>& BuildEntries()
    {
        using namespace ParameterEntryTypes;

        static std::vector<PluginParameterRegistry::Entry> entries =
        {
            // ---- Delay ----
            // TODO: make range from 1..1000
            MakeFloat(
                "delayTime",
                "Delay Time",
                juce::NormalisableRange<float>(0.0f, 1000.0f),
                300.0f,
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
                juce::NormalisableRange<float>(0.0f, 1000.0f),
                300.0f,
                [](Chronoverb& c, float v) { c.SetDuckAttack(v); }),

            MakeFloat(
                "duckRelease",
                "Duck Release",
                juce::NormalisableRange<float>(0.0f, 1000.0f),
                300.0f,
                [](Chronoverb& c, float v) { c.SetDuckRelease(v); }),

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

            MakeFloat(
                "pitchWetMix",
                "Pitch Shift Wet Mix",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.0f,
                [](Chronoverb& c, float v) { c.SetpitchWetMix(v); })
        };

        AddDistortionModuleEntries(entries, 1);
        AddDistortionModuleEntries(entries, 2);
        AddDistortionModuleEntries(entries, 3);

        return entries;
    }

    inline void AddDistortionModuleEntries(std::vector<PluginParameterRegistry::Entry>& entries, int moduleIndex)
    {
        const juce::String index = juce::String(moduleIndex);
        const juce::String prefix = "distortionMod" + index;

        entries.push_back(PluginParameterRegistry::Entry
        {
            prefix + "Enabled",
            "Distortion Module " + index + " Enabled",
            [prefix]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterBool>(
                    prefix + "Enabled",
                    "Distortion Module " + juce::String(prefix.getTrailingIntValue()) + " Enabled",
                    true);
            },
            [prefix, moduleIndex](Chronoverb& chronoverb,
                                  juce::AudioProcessorValueTreeState& apvts,
                                  const juce::String&)
            {
                juce::ignoreUnused(chronoverb);

                if (auto* raw = apvts.getRawParameterValue(prefix + "Enabled"))
                    chronoverb.SetDistortionModuleEnabled(moduleIndex - 1, raw->load() >= 0.5f);
            }
        });

        entries.push_back(PluginParameterRegistry::Entry
        {
            prefix + "Type",
            "Distortion Module " + index + " Type",
            [prefix, index]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterChoice>(
                    prefix + "Type",
                    "Distortion Module " + index + " Type",
                    juce::StringArray{ "Heat", "Chebyshev", "Hard Clip", "Tube" },
                    0);
            },
            [prefix, moduleIndex](Chronoverb& chronoverb,
                      juce::AudioProcessorValueTreeState& apvts,
                      const juce::String&)
            {
                if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(prefix + "Type")))
                    chronoverb.SetDistortionModuleType(moduleIndex - 1, parameter->getIndex());
            }
        });

        entries.push_back(PluginParameterRegistry::Entry
        {
            prefix + "Target",
            "Distortion Module " + index + " Target",
            [prefix, index]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterChoice>(
                    prefix + "Target",
                    "Distortion Module " + index + " Target",
                    juce::StringArray{ "Dry", "Wet", "Both" },
                    1);
            },
            [prefix, moduleIndex](Chronoverb& chronoverb,
                      juce::AudioProcessorValueTreeState& apvts,
                      const juce::String&)
            {
                if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(prefix + "Target")))
                    chronoverb.SetDistortionModuleTarget(moduleIndex - 1, parameter->getIndex());
            }
        });

        entries.push_back(PluginParameterRegistry::Entry
        {
            prefix + "Drive",
            "Distortion Module " + index + " Drive",
            [prefix, index]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterFloat>(
                    prefix + "Drive",
                    "Distortion Module " + index + " Drive",
                    juce::NormalisableRange<float>(0.0f, 1.0f),
                    0.5f);
            },
            [prefix, moduleIndex](Chronoverb& chronoverb,
                                  juce::AudioProcessorValueTreeState& apvts,
                                  const juce::String&)
            {
                if (auto* raw = apvts.getRawParameterValue(prefix + "Drive"))
                    chronoverb.SetDistortionModuleDrive(moduleIndex - 1, raw->load());
            }
        });

        entries.push_back(PluginParameterRegistry::Entry
        {
            prefix + "Mix",
            "Distortion Module " + index + " Mix",
            [prefix, index]() -> std::unique_ptr<juce::RangedAudioParameter>
            {
                return std::make_unique<juce::AudioParameterFloat>(
                    prefix + "Mix",
                    "Distortion Module " + index + " Mix",
                    juce::NormalisableRange<float>(0.0f, 1.0f),
                    1.0f);
            },
            [prefix, moduleIndex](Chronoverb& chronoverb,
                      juce::AudioProcessorValueTreeState& apvts,
                      const juce::String&)
            {
                if (auto* raw = apvts.getRawParameterValue(prefix + "Mix"))
                    chronoverb.SetDistortionModuleMix(moduleIndex - 1, raw->load());
            }
        });
    }
}