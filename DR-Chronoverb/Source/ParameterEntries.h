#pragma once

#include "ParameterEntryTypes.h"
#include "Filters/Chronoverb.h"

namespace ParameterEntries
{
    inline const std::vector<PluginParameterRegistry::Entry>& BuildEntries()
    {
        using namespace ParameterEntryTypes;

        static const std::vector<PluginParameterRegistry::Entry> entries =
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
                    // TODO: Hook up later when Chronoverb exposes SetDuckAttack(...)
                }),

            MakeFloat(
                "duckRelease",
                "Duck Release",
                juce::NormalisableRange<float>(0.0f, 1.0f),
                0.3f,
                [](Chronoverb&, float)
                {
                    // TODO: Hook up later when Chronoverb exposes SetDuckRelease(...)
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

            // ---- Distortion ----

        };

        AddDistortionModuleEntries(entries, 1);
        AddDistortionModuleEntries(entries, 2);
        AddDistortionModuleEntries(entries, 3);

        return entries;
    }

    inline void AddDistortionModuleEntries(std::vector<PluginParameterRegistry::Entry>& entries,
                                    int moduleIndex)
    {
        const juce::String index = juce::String(moduleIndex);
        const juce::String prefix = "distortionMod" + index;

        entries.push_back(ParameterEntryTypes::MakeBool(
            prefix + "Enabled",
            "Distortion Module " + index + " Enabled",
            true,
            [](Chronoverb& chronoverb, bool value)
            {
                juce::ignoreUnused(chronoverb, value);
                // Hook up later when module-enable DSP behavior exists.
            }));

        entries.push_back(ParameterEntryTypes::MakeChoice(
            prefix + "Type",
            "Distortion Module " + index + " Type",
            juce::StringArray{ "Heat", "Chebyshev", "Hard Clip", "Tube" },
            0,
            [](Chronoverb& chronoverb, int value)
            {
                juce::ignoreUnused(chronoverb, value);
                // Hook up later when per-module distortion type routing exists.
            }));

        entries.push_back(ParameterEntryTypes::MakeFloat(
            prefix + "Drive",
            "Distortion Module " + index + " Drive",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.5f,
            [](Chronoverb& chronoverb, float value)
            {
                juce::ignoreUnused(chronoverb, value);
                // Hook up later when per-module drive DSP exists.
            }));

        entries.push_back(ParameterEntryTypes::MakeFloat(
            prefix + "Mix",
            "Distortion Module " + index + " Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f),
            1.0f,
            [](Chronoverb& chronoverb, float value)
            {
                juce::ignoreUnused(chronoverb, value);
                // Hook up later when per-module mix DSP exists.
            }));
    }
}