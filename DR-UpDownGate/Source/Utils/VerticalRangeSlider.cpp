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

void VerticalRangeSlider::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds();
    int sliderX = bounds.getCentreX();

    // Draw track
    graphics.setColour(juce::Colours::darkgrey);
    graphics.drawLine(sliderX, valueToY(minValue), sliderX, valueToY(maxValue), 4.0f);

    // Highlighted range
    graphics.setColour(juce::Colours::skyblue);
    graphics.drawLine(sliderX, valueToY(upperValue), sliderX, valueToY(lowerValue), 8.0f);

    // Draw thumbs
    graphics.setColour(juce::Colours::white);
    graphics.fillEllipse(sliderX - thumbRadius, valueToY(lowerValue) - thumbRadius,
                  thumbRadius * 2, thumbRadius * 2);

    graphics.fillEllipse(sliderX - thumbRadius, valueToY(upperValue) - thumbRadius,
                  thumbRadius * 2, thumbRadius * 2);
}

void VerticalRangeSlider::resized() {}

int VerticalRangeSlider::valueToY(float value) const
{
    // Map value to Y in component
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

    if (std::abs(mouseY - lowerY) < thumbRadius * 2)
        dragging = Lower;
    else if (std::abs(mouseY - upperY) < thumbRadius * 2)
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