#include "HorizontalRangeSlider.h"
#include "Theme.h"

HorizontalRangeSlider::HorizontalRangeSlider(float MinimumValue, float MaximumValue)
    : minValue(MinimumValue), maxValue(MaximumValue), lowerValue(MinimumValue), upperValue(MaximumValue)
{
}

void HorizontalRangeSlider::setLowerValue(float NewValue)
{
    float clampedValue = juce::jlimit(minValue, upperValue, NewValue);

    if (lowerValue != clampedValue)
    {
        lowerValue = clampedValue;

        if (OnLowerValueChanged)
        {
            OnLowerValueChanged(lowerValue);
        }

        repaint();
    }
}

void HorizontalRangeSlider::setUpperValue(float NewValue)
{
    float clampedValue = juce::jlimit(lowerValue, maxValue, NewValue);

    if (upperValue != clampedValue)
    {
        upperValue = clampedValue;

        if (OnUpperValueChanged)
        {
            OnUpperValueChanged(upperValue);
        }

        repaint();
    }
}

void HorizontalRangeSlider::setRoundness(float Radius)
{
    roundness = Radius;
    repaint();
}

void HorizontalRangeSlider::paint(juce::Graphics& Graphics)
{
    auto bounds = getLocalBounds().toFloat();

    // Draw track background (full component area)
    Graphics.setColour(AccentGray);
    Graphics.fillRoundedRectangle(bounds, roundness);

    // Calculate range rectangle
    float sliderHeight = bounds.getHeight();
    float sliderY = bounds.getY() + (bounds.getHeight() - sliderHeight) / 2.0f;
    float lowerX = valueToX(lowerValue);
    float upperX = valueToX(upperValue);

    float rangeWidth = upperX - lowerX;
    juce::Rectangle<float> rangeRect(lowerX, sliderY, rangeWidth, sliderHeight);

    // Draw range rectangle
    Graphics.setColour(ThemePink);
    Graphics.fillRoundedRectangle(rangeRect, roundness);

    // Draw handles (flat lines)
    Graphics.setColour(ThemePink.darker(0.2f));
    float handleInset = handleMargin;
    float handleY1 = rangeRect.getY() + handleInset;
    float handleY2 = rangeRect.getBottom() - handleInset;

    // Left handle (lowerValue)
    Graphics.drawLine(lowerX + handleMargin, handleY1, lowerX + handleMargin, handleY2, (float)handleThickness);

    // Right handle (upperValue)
    Graphics.drawLine(upperX - handleMargin, handleY1, upperX - handleMargin, handleY2, (float)handleThickness);
}

void HorizontalRangeSlider::resized()
{
    // No layout needed, everything drawn relative to bounds
}

int HorizontalRangeSlider::valueToX(float Value) const
{
    auto bounds = getLocalBounds();
    float proportion = (Value - minValue) / (maxValue - minValue);

    return juce::jmap(proportion, float(bounds.getX()), float(bounds.getRight()));
}

float HorizontalRangeSlider::xToValue(int X) const
{
    auto bounds = getLocalBounds();
    float proportion = (float)(X - bounds.getX()) / bounds.getWidth();

    return juce::jlimit(minValue, maxValue, minValue + proportion * (maxValue - minValue));
}

void HorizontalRangeSlider::mouseDown(const juce::MouseEvent& MouseEvent)
{
    int mouseX = MouseEvent.getPosition().getX();
    int lowerX = valueToX(lowerValue);
    int upperX = valueToX(upperValue);

    // Use a tolerance for the handle lines
    int tolerance = 20;

    if (std::abs(mouseX - lowerX) < tolerance)
    {
        dragging = Lower;
    }
    else if (std::abs(mouseX - upperX) < tolerance)
    {
        dragging = Upper;
    }
    else
    {
        dragging = None;
    }
}

void HorizontalRangeSlider::mouseDrag(const juce::MouseEvent& MouseEvent)
{
    int mouseX = MouseEvent.getPosition().getX();
    float value = xToValue(mouseX);

    if (dragging == Lower)
    {
        setLowerValue(value);
    }
    else if (dragging == Upper)
    {
        setUpperValue(value);
    }
}

void HorizontalRangeSlider::mouseMove(const juce::MouseEvent& MouseEvent)
{
    int mouseX = MouseEvent.getPosition().getX();
    int lowerX = valueToX(lowerValue);
    int upperX = valueToX(upperValue);

    int tolerance = 20;

    if (std::abs(mouseX - lowerX) < tolerance || std::abs(mouseX - upperX) < tolerance)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}