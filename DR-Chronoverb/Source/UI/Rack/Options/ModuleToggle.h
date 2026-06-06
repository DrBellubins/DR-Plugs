#pragma once

#include "ModuleOption.h"
#include <juce_audio_processors/juce_audio_processors.h>

class ModuleToggle : public ModuleOption
{
public:
    using Attachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    ModuleToggle();
    ~ModuleToggle() override = default;

    void AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& parameterID);

    juce::ToggleButton& GetButton();

    void SetControlBounds(const juce::Rectangle<int>& newBounds);
    juce::Rectangle<int> GetControlBounds() const;

    void SetLabelHeight(int newLabelHeight);
    int GetLabelHeight() const;

    void ApplyTheme(const RackTheme& rackTheme) override;
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton button;
    std::unique_ptr<Attachment> attachment;

    juce::Rectangle<int> controlBoundsOverride;
    int labelHeight = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleToggle)
};