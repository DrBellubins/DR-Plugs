#pragma once

#include "ModuleOption.h"
#include <juce_audio_processors/juce_audio_processors.h>

class ModuleKnob : public ModuleOption
{
public:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    ModuleKnob();
    ~ModuleKnob() override = default;

    void AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& parameterID);

    juce::Slider& GetSlider();

    void SetSliderBounds(const juce::Rectangle<int>& newBounds);
    juce::Rectangle<int> GetSliderBounds() const;

    void SetLabelHeight(int newLabelHeight);
    int GetLabelHeight() const;

    void ApplyTheme(const RackTheme& rackTheme) override;
    void resized() override;

private:
    juce::Slider slider;
    std::unique_ptr<Attachment> attachment;

    juce::Rectangle<int> sliderBoundsOverride;
    int labelHeight = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleKnob)
};