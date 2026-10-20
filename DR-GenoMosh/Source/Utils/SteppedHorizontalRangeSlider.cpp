#include "SteppedHorizontalRangeSlider.h"

SteppedHorizontalRangeSlider::SteppedHorizontalRangeSlider(float MinimumValue, float MaximumValue, float StepSize)
    : HorizontalRangeSlider(MinimumValue, MaximumValue), stepSize(StepSize)
{
}

void SteppedHorizontalRangeSlider::setStepSize(float NewStepSize)
{
    stepSize = NewStepSize;

    // Quantize existing values to new step
    setLowerValue(getLowerValue());
    setUpperValue(getUpperValue());
}

float SteppedHorizontalRangeSlider::getStepSize() const
{
    return stepSize;
}

void SteppedHorizontalRangeSlider::setLowerValue(float NewValue)
{
    float QuantizedValue = quantizeToStep(NewValue);
    HorizontalRangeSlider::setLowerValue(QuantizedValue);
}

void SteppedHorizontalRangeSlider::setUpperValue(float NewValue)
{
    float QuantizedValue = quantizeToStep(NewValue);
    HorizontalRangeSlider::setUpperValue(QuantizedValue);
}

void SteppedHorizontalRangeSlider::mouseDrag(const juce::MouseEvent& MouseEvent)
{
    int mouseX = MouseEvent.getPosition().getX();
    float value = xToValue(mouseX);
    float quantized = quantizeToStep(value);

    if (dragging == Lower)
    {
        if (quantized > getUpperValue())
        {
            quantized = getUpperValue();
        }
        setLowerValue(quantized);
    }
    else if (dragging == Upper)
    {
        if (quantized < getLowerValue())
        {
            quantized = getLowerValue();
        }
        setUpperValue(quantized);
    }
}

float SteppedHorizontalRangeSlider::quantizeToStep(float Value) const
{
    float Min = getMinValue();
    float Max = getMaxValue();
    float Clamped = juce::jlimit(Min, Max, Value);
    float N = std::round((Clamped - Min) / stepSize);
    return juce::jlimit(Min, Max, Min + N * stepSize);
}

float SteppedHorizontalRangeSlider::getMinValue() const
{
    return static_cast<const HorizontalRangeSlider*>(this)->minValue;
}

float SteppedHorizontalRangeSlider::getMaxValue() const
{
    return static_cast<const HorizontalRangeSlider*>(this)->maxValue;
}