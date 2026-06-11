#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <vector>

class Chronoverb;

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
};