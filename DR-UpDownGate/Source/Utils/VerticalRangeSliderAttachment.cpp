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

    rangeSlider.OnLowerValueChanged = [this](float newLowerValue)
    {
        if (!updatingSlider)
        {
            updatingParameter = true;
            *valueTreeState.getRawParameterValue(lowerID) = newLowerValue;
            updatingParameter = false;
        }
    };

    rangeSlider.OnUpperValueChanged = [this](float newUpperValue)
    {
        if (!updatingSlider)
        {
            updatingParameter = true;
            *valueTreeState.getRawParameterValue(upperID) = newUpperValue;
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
    juce::MessageManagerLock lock;

    if (!updatingParameter)
    {
        updatingSlider = true;
        updateSliderFromParameters();
        updatingSlider = false;
    }
}