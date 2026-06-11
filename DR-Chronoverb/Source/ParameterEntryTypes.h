#pragma once

#include "PluginParameterRegistry.h"

class Chronoverb;

namespace ParameterEntryTypes
{
    PluginParameterRegistry::Entry MakeFloat(
        const juce::String& parameterID,
        const juce::String& parameterName,
        const juce::NormalisableRange<float>& range,
        float defaultValue,
        std::function<void(Chronoverb&, float)> apply);

    PluginParameterRegistry::Entry MakeChoice(
        const juce::String& parameterID,
        const juce::String& parameterName,
        const juce::StringArray& choices,
        int defaultIndex,
        std::function<void(Chronoverb&, int)> apply);

    PluginParameterRegistry::Entry MakeBool(
        const juce::String& parameterID,
        const juce::String& parameterName,
        bool defaultValue,
        std::function<void(Chronoverb&, bool)> apply);
}