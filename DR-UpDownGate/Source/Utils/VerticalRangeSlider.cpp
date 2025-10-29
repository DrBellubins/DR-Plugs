#include "VerticalRangeSlider.h"

VerticalRangeSlider::VerticalRangeSlider(float min, float max)
    : minValue(min), maxValue(max), lowerValue(min), upperValue(max)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void VerticalRangeSlider::setLowerValue(float value)
{
    lowerValue = juce::jlimit(minValue, upperValue, value);
    repaint();
}

void VerticalRangeSlider::setUpperValue(float value)
{
    upperValue = juce::jlimit(lowerValue, maxValue, value);
    repaint();
}

void VerticalRangeSlider::setRoundness(float radius)
{
    roundness = radius;
    repaint();
}

void VerticalRangeSlider::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Draw track background (full component area)
    g.setColour(juce::Colour(0xFF2D2D2D)); // dark grey
    g.fillRoundedRectangle(bounds, roundness);

    // Calculate range rectangle
    float sliderWidth = bounds.getWidth();
    float sliderX = bounds.getX() + (bounds.getWidth() - sliderWidth) / 2.0f;
    float lowerY = valueToY(lowerValue);
    float upperY = valueToY(upperValue);

    float rangeHeight = lowerY - upperY;
    juce::Rectangle<float> rangeRect(sliderX, upperY, sliderWidth, rangeHeight);

    // Draw range rectangle
    g.setColour(juce::Colour(0xFFFF8FE5)); // pink
    g.fillRoundedRectangle(rangeRect, roundness);

    // Draw handles (flat lines)
    g.setColour(juce::Colour(0xFFFF8FE5).darker(0.2f));
    float handleInset = handleMargin;
    float handleX1 = rangeRect.getX() + handleInset;
    float handleX2 = rangeRect.getRight() - handleInset;

    // Top handle (upperValue)
    g.drawLine(handleX1, upperY + handleMargin, handleX2, upperY + handleMargin, (float)handleThickness);

    // Bottom handle (lowerValue)
    g.drawLine(handleX1, lowerY - handleMargin, handleX2, lowerY - handleMargin, (float)handleThickness);
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

void VerticalRangeSlider::mouseDown(const juce::MouseEvent& e)
{
    int mouseY = e.getPosition().getY();
    int lowerY = valueToY(lowerValue);
    int upperY = valueToY(upperValue);

    // Use a tolerance for the handle lines
    int tolerance = 12;
    if (std::abs(mouseY - lowerY) < tolerance)
        dragging = Lower;
    else if (std::abs(mouseY - upperY) < tolerance)
        dragging = Upper;
    else
        dragging = None;
}

void VerticalRangeSlider::mouseDrag(const juce::MouseEvent& e)
{
    int mouseY = e.getPosition().getY();
    float value = yToValue(mouseY);

    if (dragging == Lower)
        setLowerValue(value);
    else if (dragging == Upper)
        setUpperValue(value);
}