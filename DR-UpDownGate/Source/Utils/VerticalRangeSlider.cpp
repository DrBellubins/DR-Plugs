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
        notifyTooltipStateChanged();
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
        notifyTooltipStateChanged();
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
    notifyTooltipStateChanged();
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
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    float thumbXPosition = rangeRectangle.getCentreX() - (thumbWidth * 0.5f);
    float normalThumbYPosition = rangeRectangle.getY() + thumbTopInset;

    float upperThumbYPosition = normalThumbYPosition;
    float lowerThumbNormalYPosition = rangeRectangle.getBottom() - thumbTopInset - thumbHeight;

    float availableSpacing = lowerThumbNormalYPosition - normalThumbYPosition;

    if (availableSpacing < visualMinimumThumbSpacing)
    {
        float rangeCentreYPosition = rangeRectangle.getCentreY();
        float halfVisualSpacing = (visualMinimumThumbSpacing + thumbHeight) * 0.5f;

        upperThumbYPosition = rangeCentreYPosition - halfVisualSpacing;
        upperThumbYPosition = juce::jlimit(bounds.getY(), bounds.getBottom() - thumbHeight, upperThumbYPosition);
    }

    return juce::Rectangle<float>(thumbXPosition, upperThumbYPosition, thumbWidth, thumbHeight);
}

juce::Rectangle<float> VerticalRangeSlider::getLowerThumbRectangle() const
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    float thumbXPosition = rangeRectangle.getCentreX() - (thumbWidth * 0.5f);
    float upperThumbNormalYPosition = rangeRectangle.getY() + thumbTopInset;
    float normalThumbYPosition = rangeRectangle.getBottom() - thumbTopInset - thumbHeight;

    float lowerThumbYPosition = normalThumbYPosition;
    float availableSpacing = normalThumbYPosition - upperThumbNormalYPosition;

    if (availableSpacing < visualMinimumThumbSpacing)
    {
        float rangeCentreYPosition = rangeRectangle.getCentreY();
        float halfVisualSpacing = (visualMinimumThumbSpacing + thumbHeight) * 0.5f;

        lowerThumbYPosition = rangeCentreYPosition + halfVisualSpacing - thumbHeight;
        lowerThumbYPosition = juce::jlimit(bounds.getY(), bounds.getBottom() - thumbHeight, lowerThumbYPosition);
    }

    return juce::Rectangle<float>(thumbXPosition, lowerThumbYPosition, thumbWidth, thumbHeight);
}

VerticalRangeSlider::ActiveThumb VerticalRangeSlider::getActiveThumb() const
{
    if (draggingThumb == Upper)
    {
        return UpperThumb;
    }

    if (draggingThumb == Lower)
    {
        return LowerThumb;
    }

    if (hoveredThumb == HoverUpper)
    {
        return UpperThumb;
    }

    if (hoveredThumb == HoverLower)
    {
        return LowerThumb;
    }

    return NoThumb;
}

juce::Rectangle<float> VerticalRangeSlider::getActiveThumbRectangle() const
{
    if (draggingThumb == Upper)
    {
        return getUpperThumbRectangle();
    }

    if (draggingThumb == Lower)
    {
        return getLowerThumbRectangle();
    }

    if (hoveredThumb == HoverUpper)
    {
        return getUpperThumbRectangle();
    }

    if (hoveredThumb == HoverLower)
    {
        return getLowerThumbRectangle();
    }

    return {};
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

bool VerticalRangeSlider::shouldShowTooltip() const
{
    return draggingThumb != None || hoveredThumb != HoverNone;
}

juce::Rectangle<float> VerticalRangeSlider::getActiveThumbBoundsInComponent(const juce::Component& targetComponent) const
{
    juce::Rectangle<float> activeThumbRectangle = getActiveThumbRectangle();

    if (activeThumbRectangle.isEmpty())
    {
        return {};
    }

    juce::Rectangle<int> integerThumbRectangle = activeThumbRectangle.getSmallestIntegerContainer();
    juce::Rectangle<int> targetRectangle = targetComponent.getLocalArea(this, integerThumbRectangle);

    return targetRectangle.toFloat();
}

juce::String VerticalRangeSlider::getActiveThumbTooltipText() const
{
    if (draggingThumb == Upper)
    {
        return juce::String(upperValue, 1) + " dB";
    }

    if (draggingThumb == Lower)
    {
        return juce::String(lowerValue, 1) + " dB";
    }

    if (hoveredThumb == HoverUpper)
    {
        return juce::String(upperValue, 1) + " dB";
    }

    if (hoveredThumb == HoverLower)
    {
        return juce::String(lowerValue, 1) + " dB";
    }

    return {};
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
    juce::Colour activeThumbColour = ThemePink.brighter(0.35f);

    juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle();
    juce::Rectangle<float> lowerThumbRectangle = getLowerThumbRectangle();

    bool upperThumbIsActive = (hoveredThumb == HoverUpper || draggingThumb == Upper);
    bool lowerThumbIsActive = (hoveredThumb == HoverLower || draggingThumb == Lower);

    graphics.setColour(upperThumbIsActive ? activeThumbColour : normalThumbColour);
    graphics.fillRoundedRectangle(upperThumbRectangle, thumbHeight * 0.5f);

    graphics.setColour(lowerThumbIsActive ? activeThumbColour : normalThumbColour);
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
        juce::jmap(
            1.0f - proportion,
            static_cast<float>(bounds.getY()),
            static_cast<float>(bounds.getBottom())
        )
    );
}

float VerticalRangeSlider::yToValue(int yPosition) const
{
    juce::Rectangle<int> bounds = getLocalBounds();
    float proportion = 1.0f - (static_cast<float>(yPosition - bounds.getY()) / static_cast<float>(bounds.getHeight()));

    return juce::jlimit(minValue, maxValue, minValue + proportion * (maxValue - minValue));
}

float VerticalRangeSlider::deltaYToValueDelta(float deltaY) const
{
    juce::Rectangle<int> bounds = getLocalBounds();

    if (bounds.getHeight() <= 0)
    {
        return 0.0f;
    }

    float valueRange = maxValue - minValue;
    return (-deltaY / static_cast<float>(bounds.getHeight())) * valueRange;
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

    dragStartMouseY = mouseEvent.getPosition().getY();
    dragStartLowerValue = lowerValue;
    dragStartUpperValue = upperValue;

    repaint();
    notifyTooltipStateChanged();
}

void VerticalRangeSlider::mouseDrag(const juce::MouseEvent& mouseEvent)
{
    float mouseDeltaY = static_cast<float>(mouseEvent.getPosition().getY() - dragStartMouseY);
    float valueDelta = deltaYToValueDelta(mouseDeltaY);

    if (draggingThumb == Lower)
    {
        setLowerValue(dragStartLowerValue + valueDelta);
    }
    else if (draggingThumb == Upper)
    {
        setUpperValue(dragStartUpperValue + valueDelta);
    }

    notifyTooltipStateChanged();
}

void VerticalRangeSlider::mouseMove(const juce::MouseEvent& mouseEvent)
{
    HoveredThumb newHoveredThumb = getHoveredThumbAtPosition(mouseEvent.getPosition());

    if (hoveredThumb != newHoveredThumb)
    {
        hoveredThumb = newHoveredThumb;
        repaint();
        notifyTooltipStateChanged();
    }

    if (hoveredThumb == HoverNone)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
}

void VerticalRangeSlider::mouseExit(const juce::MouseEvent& mouseEvent)
{
    juce::ignoreUnused(mouseEvent);

    hoveredThumb = HoverNone;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
    notifyTooltipStateChanged();
}

void VerticalRangeSlider::mouseUp(const juce::MouseEvent& mouseEvent)
{
    juce::ignoreUnused(mouseEvent);

    draggingThumb = None;
    repaint();
    notifyTooltipStateChanged();
}

void VerticalRangeSlider::notifyTooltipStateChanged()
{
    if (OnTooltipStateChanged)
    {
        OnTooltipStateChanged();
    }
}