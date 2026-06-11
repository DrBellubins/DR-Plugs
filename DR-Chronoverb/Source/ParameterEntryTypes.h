#pragma once

#include "PluginParameterRegistry.h"
#include "Filters/Chronoverb.h"

namespace ParameterEntryTypes
{
    inline PluginParameterRegistry::Entry MakeFloat(
        const juce::String& parameterID,
        const juce::String& parameterName,
        const juce::NormalisableRange<float>& range,
        float defaultValue,
        std::function<void(Chronoverb&, float)> apply)
    {
        return PluginParameterRegistry::Entry
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

    inline PluginParameterRegistry::Entry MakeChoice(
        const juce::String& parameterID,
        const juce::String& parameterName,
        const juce::StringArray& choices,
        int defaultIndex,
        std::function<void(Chronoverb&, int)> apply)
    {
        return PluginParameterRegistry::Entry
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

    inline PluginParameterRegistry::Entry MakeBool(
        const juce::String& parameterID,
        const juce::String& parameterName,
        bool defaultValue,
        std::function<void(Chronoverb&, bool)> apply)
    {
        return PluginParameterRegistry::Entry
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
}