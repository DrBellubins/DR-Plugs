#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <vector>

class Chronoverb;

// A single source of truth for plugin parameter definitions.
// Each entry knows:
// 1) how to create the APVTS parameter
// 2) how to apply the parameter value to Chronoverb
class PluginParameterRegistry
{
public:
    struct Entry
    {
        juce::String parameterID;
        juce::String parameterName;

        std::function<std::unique_ptr<juce::RangedAudioParameter>()> createParameter;
        std::function<void(Chronoverb&, juce::AudioProcessorValueTreeState&, const juce::String&)> applyFromState;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout CreateLayout();
    static const std::vector<Entry>& GetEntries();

    static void AddListeners(juce::AudioProcessorValueTreeState& apvts,
                             juce::AudioProcessorValueTreeState::Listener* listener);

    static void RemoveListeners(juce::AudioProcessorValueTreeState& apvts,
                                juce::AudioProcessorValueTreeState::Listener* listener);

    static void ApplyAll(Chronoverb& chronoverb,
                         juce::AudioProcessorValueTreeState& apvts);

    static bool ApplyOneIfMatched(Chronoverb& chronoverb,
                                  juce::AudioProcessorValueTreeState& apvts,
                                  const juce::String& changedParameterID);

private:
    static float ReadFloat(juce::AudioProcessorValueTreeState& apvts, const juce::String& parameterID);
    static int ReadChoiceIndex(juce::AudioProcessorValueTreeState& apvts, const juce::String& parameterID);
    static bool ReadBool(juce::AudioProcessorValueTreeState& apvts, const juce::String& parameterID);
};