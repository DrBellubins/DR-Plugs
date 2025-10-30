#include "VerticalRangeSliderAttachment.h"

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

    // Set initial slider values from parameters
    updateSliderFromParameters();

    // Start the timer to poll for value changes
    startTimerHz(30);
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

    if (!updatingParameter)
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

    // Get current parameter values
    float paramLower = valueTreeState.getRawParameterValue(lowerID)->load();
    float paramUpper = valueTreeState.getRawParameterValue(upperID)->load();

    // Update slider if it doesn't match parameter
    if (rangeSlider.getLowerValue() != paramLower)
    {
        updatingSlider = true;
        rangeSlider.setLowerValue(paramLower);
        updatingSlider = false;
    }
    if (rangeSlider.getUpperValue() != paramUpper)
    {
        updatingSlider = true;
        rangeSlider.setUpperValue(paramUpper);
        updatingSlider = false;
    }

    // Push slider changes to parameter (only if user has dragged)
    static float lastLower = paramLower;
    static float lastUpper = paramUpper;

    float sliderLower = rangeSlider.getLowerValue();
    float sliderUpper = rangeSlider.getUpperValue();

    if (sliderLower != lastLower)
    {
        updatingParameter = true;
        *valueTreeState.getRawParameterValue(lowerID) = sliderLower;
        updatingParameter = false;
        lastLower = sliderLower;
    }

    if (sliderUpper != lastUpper)
    {
        updatingParameter = true;
        *valueTreeState.getRawParameterValue(upperID) = sliderUpper;
        updatingParameter = false;
        lastUpper = sliderUpper;
    }
}

void VerticalRangeSliderAttachment::updateSliderFromParameters()
{
    float lower = valueTreeState.getRawParameterValue(lowerID)->load();
    float upper = valueTreeState.getRawParameterValue(upperID)->load();
    rangeSlider.setLowerValue(lower);
    rangeSlider.setUpperValue(upper);
}