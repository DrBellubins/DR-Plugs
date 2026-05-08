#pragma once

#include "FlatRotaryLookAndFeel.h"
#include "RoundedToggle.h"
#include "ThemedCheckbox.h"
#include "ThemedKnob.h"
#include "ThemedSlider.h"

class UIHelpers
{
public:
    UIHelpers(
        juce::Component& newParentComponent,
        juce::AudioProcessorValueTreeState& newValueTreeState,
        FlatRotaryLookAndFeel& newRotaryLookAndFeel
    );

    void CreateToggle(
        std::unique_ptr<RoundedToggle>& toggle,
        std::unique_ptr<RoundedToggle::Attachment>& attachment,
        RoundedToggle::Orientation orientation,
        const juce::String& parameterID,
        int width,
        int height,
        int offsetFromCenterX,
        int offsetFromCenterY);

    void CreateCheckbox(
        std::unique_ptr<ThemedCheckbox>& checkbox,
        std::unique_ptr<ThemedCheckbox::Attachment>& attachment,
        const juce::String& parameterID,
        int width,
        int height,
        int offsetFromCenterX,
        int offsetFromCenterY);

    void CreateSlider(
        std::unique_ptr<ThemedSlider>& slider,
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
        const juce::String& parameterID,
        int width,
        int height,
        int offsetFromCenterX,
        int offsetFromCenterY);

    void CreateSliderLabel(
        std::unique_ptr<juce::Label>& label,
        ThemedSlider& slider,
        const juce::String& text,
        float fontSize,
        int offsetX);

    void CreateLabel(
        std::unique_ptr<juce::Label>& label,
        const juce::String& text,
        float fontSize,
        int offsetFromCenterX,
        int offsetFromCenterY);

    void CreateKnob(
        std::unique_ptr<ThemedKnob>& knob,
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
        const juce::String& parameterID,
        const juce::String& suffix,
        int widthHeight,
        int offsetFromCenterX,
        int offsetFromCenterY);

    void CreateKnobLabel(
        std::unique_ptr<juce::Label>& label,
        ThemedKnob& knob,
        const juce::String& text,
        float fontSize,
        int offsetY);

    void CenterKnobLabel(
        std::unique_ptr<juce::Label>& label,
        ThemedKnob& knob,
        int offsetY);

    int GetLabelWidth(const std::unique_ptr<juce::Label>& label) const;

private:
    juce::Component& parentComponent;
    juce::AudioProcessorValueTreeState& valueTreeState;
    FlatRotaryLookAndFeel& rotaryLookAndFeel;
};