#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "VerticalRangeSlider.h"

// A custom attachment that binds two AudioProcessorValueTreeState parameters to a VerticalRangeSlider.
class VerticalRangeSliderAttachment :
    private juce::AudioProcessorValueTreeState::Listener
{
public:
    VerticalRangeSliderAttachment(
        juce::AudioProcessorValueTreeState& ParameterValueTreeState,
        const juce::String& LowerParameterID,
        const juce::String& UpperParameterID,
        VerticalRangeSlider& RangeSlider);

    ~VerticalRangeSliderAttachment() override;

private:
    juce::AudioProcessorValueTreeState& valueTreeState;
    juce::String lowerID, upperID;
    VerticalRangeSlider& rangeSlider;

    bool updatingSlider = false;
    bool updatingParameter = false;

    // Called when parameter changes (from DAW, automation, etc.)
    void parameterChanged(const juce::String& ParameterID, float NewValue) override;

    void updateSliderFromParameters();
};