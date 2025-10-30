#include "VerticalRangeSliderAttachment.h"

// Helper: Update slider from parameter values
void VerticalRangeSliderAttachment::updateSliderFromParameters()
{
    float lower = valueTreeState.getRawParameterValue(lowerID)->load();
    float upper = valueTreeState.getRawParameterValue(upperID)->load();

    rangeSlider.setLowerValue(lower);
    rangeSlider.setUpperValue(upper);
}

VerticalRangeSliderAttachment::VerticalRangeSliderAttachment(
    juce::AudioProcessorValueTreeState& ParameterValueTreeState,
    const juce::String& LowerParameterID,
    const juce::String& UpperParameterID,
    VerticalRangeSlider& RangeSlider)
    : valueTreeState(ParameterValueTreeState),
      lowerID(LowerParameterID),
      upperID(UpperParameterID),
      rangeSlider(RangeSlider)
{
    valueTreeState.addParameterListener(lowerID, this);
    valueTreeState.addParameterListener(upperID, this);

    // Set initial slider values from parameters (will be correct after DAW restores state)
    updateSliderFromParameters();

    // Attach drag callbacks: you must add these methods to your VerticalRangeSlider class!
    // (See below for the necessary additions)
    rangeSlider.OnUpperValueChanged = [this](float newLower)
    {
        if (!updatingSlider)
        {
            updatingParameter = true;
            *valueTreeState.getRawParameterValue(lowerID) = newLower;
            updatingParameter = false;
        }
    };

    rangeSlider.OnUpperValueChanged = [this](float newUpper)
    {
        if (!updatingSlider)
        {
            updatingParameter = true;
            *valueTreeState.getRawParameterValue(upperID) = newUpper;
            updatingParameter = false;
        }
    };
}

VerticalRangeSliderAttachment::~VerticalRangeSliderAttachment()
{
    valueTreeState.removeParameterListener(lowerID, this);
    valueTreeState.removeParameterListener(upperID, this);
}

void VerticalRangeSliderAttachment::parameterChanged(const juce::String& ParameterID, float NewValue)
{
    juce::MessageManagerLock lock; // Ensure thread-safe UI update

    if (!updatingParameter)
    {
        updatingSlider = true;
        updateSliderFromParameters();
        updatingSlider = false;
    }
}