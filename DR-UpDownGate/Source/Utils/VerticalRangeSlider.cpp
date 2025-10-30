#include "VerticalRangeSlider.h"
#include "Theme.h"

VerticalRangeSlider::VerticalRangeSlider(float min, float max)
    : minValue(min), maxValue(max), lowerValue(min), upperValue(max)
{
}

void VerticalRangeSlider::setLowerValue(float value)
{
    float clamped = juce::jlimit(minValue, upperValue, value);

    if (lowerValue != clamped)
    {
        lowerValue = clamped;

        if (OnLowerValueChanged)
            OnLowerValueChanged(lowerValue);

        repaint();
    }
}

void VerticalRangeSlider::setUpperValue(float value)
{
    float clamped = juce::jlimit(lowerValue, maxValue, value);

    if (upperValue != clamped)
    {
        upperValue = clamped;

        if (OnUpperValueChanged)
            OnUpperValueChanged(upperValue);

        repaint();
    }
}

void VerticalRangeSlider::setRoundness(float radius)
{
    roundness = radius;
    repaint();
}

void VerticalRangeSlider::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();

    // Draw track background (full component area)
    graphics.setColour(AccentGray);
    graphics.fillRoundedRectangle(bounds, roundness);

    // Calculate range rectangle
    float sliderWidth = bounds.getWidth();
    float sliderX = bounds.getX() + (bounds.getWidth() - sliderWidth) / 2.0f;
    float lowerY = valueToY(lowerValue);
    float upperY = valueToY(upperValue);

    float rangeHeight = lowerY - upperY;
    juce::Rectangle<float> rangeRect(sliderX, upperY, sliderWidth, rangeHeight);

    // Draw range rectangle
    graphics.setColour(ThemePink);
    graphics.fillRoundedRectangle(rangeRect, roundness);

    // Draw handles (flat lines)
    graphics.setColour(ThemePink.darker(0.2f));
    float handleInset = handleMargin;
    float handleX1 = rangeRect.getX() + handleInset;
    float handleX2 = rangeRect.getRight() - handleInset;

    // Top handle (upperValue)
    graphics.drawLine(handleX1, upperY + handleMargin, handleX2, upperY + handleMargin, (float)handleThickness);

    // Bottom handle (lowerValue)
    graphics.drawLine(handleX1, lowerY - handleMargin, handleX2, lowerY - handleMargin, (float)handleThickness);
}

void VerticalRangeSlider::resized()
{
    // No layout needed, everything drawn relative to bounds
}

int VerticalRangeSlider::valueToY(float value) const
{
    auto bounds = getLocalBounds();
    float proportion = (value - minValue) / (maxValue - minValue);
    return juce::jmap(1.0f - proportion, float(bounds.getY()), float(bounds.getBottom()));
}

float VerticalRangeSlider::yToValue(int y) const
{
    auto bounds = getLocalBounds();
    float proportion = 1.0f - ((float)(y - bounds.getY()) / bounds.getHeight());
    return juce::jlimit(minValue, maxValue, minValue + proportion * (maxValue - minValue));
}

void VerticalRangeSlider::mouseDown(const juce::MouseEvent& event)
{
    int mouseY = event.getPosition().getY();
    int lowerY = valueToY(lowerValue);
    int upperY = valueToY(upperValue);

    // Use a tolerance for the handle lines
    int tolerance = 20;

    if (std::abs(mouseY - lowerY) < tolerance)
        dragging = Lower;
    else if (std::abs(mouseY - upperY) < tolerance)
        dragging = Upper;
    else
        dragging = None;
}

void VerticalRangeSlider::mouseDrag(const juce::MouseEvent& event)
{
    int mouseY = event.getPosition().getY();
    float value = yToValue(mouseY);

    if (dragging == Lower)
        setLowerValue(value);
    else if (dragging == Upper)
        setUpperValue(value);
}

void VerticalRangeSlider::mouseMove(const juce::MouseEvent& event)
{
    int mouseY = event.getPosition().getY();
    int lowerY = valueToY(lowerValue);
    int upperY = valueToY(upperValue);

    int tolerance = 20;

    if (std::abs(mouseY - lowerY) < tolerance || std::abs(mouseY - upperY) < tolerance)
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}