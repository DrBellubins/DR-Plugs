#pragma once

#include "ModuleOption.h"
#include <juce_audio_processors/juce_audio_processors.h>

class ModuleDropdown : public ModuleOption
{
public:
    using Attachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    ModuleDropdown();
    ~ModuleDropdown() override = default;

    void AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& parameterID);

    juce::ComboBox& GetComboBox();

    void SetControlBounds(const juce::Rectangle<int>& newBounds);
    juce::Rectangle<int> GetControlBounds() const;

    void SetLabelHeight(int newLabelHeight);
    int GetLabelHeight() const;

    void ApplyTheme(const RackTheme& rackTheme) override;
    void resized() override;

private:
    juce::ComboBox comboBox;
    std::unique_ptr<Attachment> attachment;

    juce::Rectangle<int> controlBoundsOverride;
    int labelHeight = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleDropdown)
};