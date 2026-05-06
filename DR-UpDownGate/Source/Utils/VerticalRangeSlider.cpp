#include "VerticalRangeSlider.h"
#include "Theme.h"

VerticalRangeSlider::VerticalRangeSlider(float minimumValue, float maximumValue)
    : minValue(minimumValue),
      maxValue(maximumValue),
      lowerValue(minimumValue),
      upperValue(maximumValue)
{
}

void VerticalRangeSlider::setLowerValue(float newValue)
{
    float maximumLowerValue = upperValue - minimumRange;
    float clampedValue = juce::jlimit(minValue, maximumLowerValue, newValue);

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

void VerticalRangeSlider::setUpperValue(float newValue)
{
    float minimumUpperValue = lowerValue + minimumRange;
    float clampedValue = juce::jlimit(minimumUpperValue, maxValue, newValue);

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

void VerticalRangeSlider::setRoundness(float newRadius)
{
    roundness = newRadius;
    repaint();
}

void VerticalRangeSlider::setMinimumRange(float newMinimumRange)
{
    minimumRange = juce::jmax(0.0f, newMinimumRange);

    if ((upperValue - lowerValue) < minimumRange)
    {
        upperValue = juce::jmin(maxValue, lowerValue + minimumRange);

        if ((upperValue - lowerValue) < minimumRange)
        {
            lowerValue = juce::jmax(minValue, upperValue - minimumRange);
        }
    }

    repaint();
}

juce::Rectangle<float> VerticalRangeSlider::getRangeRectangle() const
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    float upperYPosition = static_cast<float>(valueToY(upperValue));
    float lowerYPosition = static_cast<float>(valueToY(lowerValue));
    float rangeHeight = juce::jmax(1.0f, lowerYPosition - upperYPosition);

    return juce::Rectangle<float>(
        bounds.getX(),
        upperYPosition,
        bounds.getWidth(),
        rangeHeight
    );
}

juce::Rectangle<float> VerticalRangeSlider::getUpperThumbRectangle() const
{
    juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    float thumbXPosition = rangeRectangle.getCentreX() - (thumbWidth * 0.5f);
    float thumbYPosition = rangeRectangle.getY() + thumbTopInset;

    return juce::Rectangle<float>(thumbXPosition, thumbYPosition, thumbWidth, thumbHeight);
}

juce::Rectangle<float> VerticalRangeSlider::getLowerThumbRectangle() const
{
    juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    float thumbXPosition = rangeRectangle.getCentreX() - (thumbWidth * 0.5f);
    float thumbYPosition = rangeRectangle.getBottom() - thumbTopInset - thumbHeight;

    juce::Rectangle<float> lowerThumbRectangle(thumbXPosition, thumbYPosition, thumbWidth, thumbHeight);
    juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle();

    if (lowerThumbRectangle.getY() < upperThumbRectangle.getBottom() + visualMinimumThumbSpacing)
    {
        lowerThumbRectangle.setY(upperThumbRectangle.getBottom() + visualMinimumThumbSpacing);
    }

    return lowerThumbRectangle;
}

VerticalRangeSlider::HoveredThumb VerticalRangeSlider::getHoveredThumbAtPosition(juce::Point<int> mousePosition) const
{
    juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle().expanded(6.0f, 6.0f);
    juce::Rectangle<float> lowerThumbRectangle = getLowerThumbRectangle().expanded(6.0f, 6.0f);

    if (upperThumbRectangle.contains(mousePosition.toFloat()))
    {
        return HoverUpper;
    }

    if (lowerThumbRectangle.contains(mousePosition.toFloat()))
    {
        return HoverLower;
    }

    return HoverNone;
}

void VerticalRangeSlider::paint(juce::Graphics& graphics)
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    graphics.setColour(AccentGray);
    graphics.fillRoundedRectangle(bounds, roundness);

    juce::Rectangle<float> rangeRectangle = getRangeRectangle();
    graphics.setColour(ThemePink);
    graphics.fillRoundedRectangle(rangeRectangle, roundness);

    juce::Colour normalThumbColour = ThemePink.darker(0.2f);
    juce::Colour hoveredThumbColour = ThemePink.brighter(0.35f);

    juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle();
    juce::Rectangle<float> lowerThumbRectangle = getLowerThumbRectangle();

    graphics.setColour(hoveredThumb == HoverUpper ? hoveredThumbColour : normalThumbColour);
    graphics.fillRoundedRectangle(upperThumbRectangle, thumbHeight * 0.5f);

    graphics.setColour(hoveredThumb == HoverLower ? hoveredThumbColour : normalThumbColour);
    graphics.fillRoundedRectangle(lowerThumbRectangle, thumbHeight * 0.5f);
}

void VerticalRangeSlider::resized()
{
}

int VerticalRangeSlider::valueToY(float value) const
{
    juce::Rectangle<int> bounds = getLocalBounds();
    float proportion = (value - minValue) / (maxValue - minValue);

    return juce::roundToInt(
        juce::jmap(1.0f - proportion, static_cast<float>(bounds.getY()), static_cast<float>(bounds.getBottom()))
    );
}

float VerticalRangeSlider::yToValue(int yPosition) const
{
    juce::Rectangle<int> bounds = getLocalBounds();
    float proportion = 1.0f - (static_cast<float>(yPosition - bounds.getY()) / static_cast<float>(bounds.getHeight()));

    return juce::jlimit(minValue, maxValue, minValue + proportion * (maxValue - minValue));
}

void VerticalRangeSlider::mouseDown(const juce::MouseEvent& mouseEvent)
{
    hoveredThumb = getHoveredThumbAtPosition(mouseEvent.getPosition());

    if (hoveredThumb == HoverLower)
    {
        draggingThumb = Lower;
    }
    else if (hoveredThumb == HoverUpper)
    {
        draggingThumb = Upper;
    }
    else
    {
        draggingThumb = None;
    }

    repaint();
}

void VerticalRangeSlider::mouseDrag(const juce::MouseEvent& mouseEvent)
{
    float newValue = yToValue(mouseEvent.getPosition().getY());

    if (draggingThumb == Lower)
    {
        setLowerValue(newValue);
    }
    else if (draggingThumb == Upper)
    {
        setUpperValue(newValue);
    }
}

void VerticalRangeSlider::mouseMove(const juce::MouseEvent& mouseEvent)
{
    HoveredThumb newHoveredThumb = getHoveredThumbAtPosition(mouseEvent.getPosition());

    if (hoveredThumb != newHoveredThumb)
    {
        hoveredThumb = newHoveredThumb;
        repaint();
    }

    if (hoveredThumb == HoverNone)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
}

void VerticalRangeSlider::mouseExit(const juce::MouseEvent& mouseEvent)
{
    juce::ignoreUnused(mouseEvent);

    hoveredThumb = HoverNone;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
}

void VerticalRangeSlider::mouseUp(const juce::MouseEvent& mouseEvent)
{
    juce::ignoreUnused(mouseEvent);

    draggingThumb = None;
}