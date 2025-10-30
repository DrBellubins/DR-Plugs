#include "VerticalRangeSliderAttachment.h"

// A custom attachment that binds two AudioProcessorValueTreeState parameters to a VerticalRangeSlider.
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
    // Listen for parameter changes
    valueTreeState.addParameterListener(lowerID, this);
    valueTreeState.addParameterListener(upperID, this);

    // Set initial slider values from parameters
    updateSliderFromParameters();

    // Listen for slider changes using a timer, or you can add custom listeners
    startTimerHz(30); // Check for UI changes at 30 Hz
}

VerticalRangeSliderAttachment::~VerticalRangeSliderAttachment()
{
    valueTreeState.removeParameterListener(lowerID, this);
    valueTreeState.removeParameterListener(upperID, this);
    stopTimer();
}

void VerticalRangeSliderAttachment::parameterChanged(const juce::String& ParameterID, float NewValue)
{
    juce::MessageManagerLock lock; // UI thread required

    if (! updatingParameter)
    {
        updatingSlider = true;
        updateSliderFromParameters();
        updatingSlider = false;
    }
}

void VerticalRangeSliderAttachment::timerCallback()
{
    if (updatingSlider)
        return; // Prevent feedback loop

    // Always update slider from parameter state, so GUI reflects loaded project
    float lower = valueTreeState.getRawParameterValue(lowerID)->load();
    float upper = valueTreeState.getRawParameterValue(upperID)->load();

    if (rangeSlider.getLowerValue() != lower)
    {
        updatingSlider = true;
        rangeSlider.setLowerValue(lower);
        updatingSlider = false;
    }

    if (rangeSlider.getUpperValue() != upper)
    {
        updatingSlider = true;
        rangeSlider.setUpperValue(upper);
        updatingSlider = false;
    }

    // Now detect user changes and update parameter values
    static float lastLower = -1000.0f, lastUpper = -1000.0f;
    float currentLower = rangeSlider.getLowerValue();
    float currentUpper = rangeSlider.getUpperValue();

    if (currentLower != lastLower)
    {
        updatingParameter = true;
        *valueTreeState.getRawParameterValue(lowerID) = currentLower;
        updatingParameter = false;
        lastLower = currentLower;
    }

    if (currentUpper != lastUpper)
    {
        updatingParameter = true;
        *valueTreeState.getRawParameterValue(upperID) = currentUpper;
        updatingParameter = false;
        lastUpper = currentUpper;
    }
}

void VerticalRangeSliderAttachment::updateSliderFromParameters()
{
    float lower = valueTreeState.getRawParameterValue(lowerID)->load();
    float upper = valueTreeState.getRawParameterValue(upperID)->load();

    rangeSlider.setLowerValue(lower);
    rangeSlider.setUpperValue(upper);
}