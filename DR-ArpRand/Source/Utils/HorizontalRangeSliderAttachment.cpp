#include "HorizontalRangeSliderAttachment.h"

HorizontalRangeSliderAttachment::HorizontalRangeSliderAttachment(
    juce::AudioProcessorValueTreeState& ParameterValueTreeState,
    const juce::String& LowerParameterID,
    const juce::String& UpperParameterID,
    HorizontalRangeSlider& RangeSlider
)
    : valueTreeState(ParameterValueTreeState)
    , lowerID(LowerParameterID)
    , upperID(UpperParameterID)
    , rangeSlider(RangeSlider)
{
    valueTreeState.addParameterListener(lowerID, this);
    valueTreeState.addParameterListener(upperID, this);

    // Update slider from parameter values initially
    updateSliderFromParameters();

    rangeSlider.OnLowerValueChanged = [this](float NewValue)
    {
    	DBG("lower value updated" << NewValue << "\n");

        if (!updatingParameter)
        {
            updatingSlider = true;
            if (auto* Parameter = valueTreeState.getParameter(lowerID))
            {
                Parameter->beginChangeGesture();
                Parameter->setValueNotifyingHost(Parameter->convertTo0to1(NewValue));
                Parameter->endChangeGesture();
            }
            updatingSlider = false;
        }
    };

    rangeSlider.OnUpperValueChanged = [this](float NewValue)
    {
    	DBG("upper value updated" << NewValue << "\n");

        if (!updatingParameter)
        {
            updatingSlider = true;
            if (auto* Parameter = valueTreeState.getParameter(upperID))
            {
                Parameter->beginChangeGesture();
                Parameter->setValueNotifyingHost(Parameter->convertTo0to1(NewValue));
                Parameter->endChangeGesture();
            }
            updatingSlider = false;
        }
    };
}

HorizontalRangeSliderAttachment::~HorizontalRangeSliderAttachment()
{
    valueTreeState.removeParameterListener(lowerID, this);
    valueTreeState.removeParameterListener(upperID, this);
}

void HorizontalRangeSliderAttachment::parameterChanged(const juce::String& ParameterID, float NewValue)
{
    if (!updatingSlider)
    {
        updatingParameter = true;
        updateSliderFromParameters();
        updatingParameter = false;
    }
}

void HorizontalRangeSliderAttachment::updateSliderFromParameters()
{
    if (auto* LowerParameter = valueTreeState.getParameter(lowerID))
    {
        float LowerValue = LowerParameter->convertFrom0to1(LowerParameter->getValue());
        rangeSlider.setLowerValue(LowerValue);
    }

    if (auto* UpperParameter = valueTreeState.getParameter(upperID))
    {
        float UpperValue = UpperParameter->convertFrom0to1(UpperParameter->getValue());
        rangeSlider.setUpperValue(UpperValue);
    }
}