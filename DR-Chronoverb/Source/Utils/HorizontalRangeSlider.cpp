#include "HorizontalRangeSlider.h"
#include "Theme.h"

HorizontalRangeSlider::HorizontalRangeSlider(float minimumValue, float maximumValue)
    : minValue(minimumValue),
      maxValue(maximumValue),
      lowerValue(minimumValue),
      upperValue(maximumValue)
{
}

void HorizontalRangeSlider::setLowerValue(float newValue)
{
    float snappedValue = snapValueToStep(newValue);
    float maximumLowerValue = upperValue - minimumRange;
    float clampedValue = juce::jlimit(minValue, maximumLowerValue, snappedValue);

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

void HorizontalRangeSlider::setUpperValue(float newValue)
{
    float snappedValue = snapValueToStep(newValue);
    float minimumUpperValue = lowerValue + minimumRange;
    float clampedValue = juce::jlimit(minimumUpperValue, maxValue, snappedValue);

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

void HorizontalRangeSlider::setRoundness(float newRadius)
{
    roundness = newRadius;
    repaint();
}

void HorizontalRangeSlider::setMinimumRange(float newMinimumRange)
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

void HorizontalRangeSlider::setStepSize(float newStepSize)
{
    stepSize = juce::jmax(0.000001f, newStepSize);

    if (steppingEnabled)
    {
        setLowerValue(lowerValue);
        setUpperValue(upperValue);
    }
}

void HorizontalRangeSlider::setSteppingEnabled(bool shouldEnableStepping)
{
    steppingEnabled = shouldEnableStepping;

    if (steppingEnabled)
    {
        setLowerValue(lowerValue);
        setUpperValue(upperValue);
    }
}

bool HorizontalRangeSlider::shouldShowTooltip() const
{
    return draggingThumb != None || hoveredThumb != HoverNone;
}

HorizontalRangeSlider::ActiveThumb HorizontalRangeSlider::getActiveThumb() const
{
    if (draggingThumb == Lower)
    {
        return LowerThumb;
    }

    if (draggingThumb == Upper)
    {
        return UpperThumb;
    }

    if (hoveredThumb == HoverLower)
    {
        return LowerThumb;
    }

    if (hoveredThumb == HoverUpper)
    {
        return UpperThumb;
    }

    return NoThumb;
}

juce::Rectangle<float> HorizontalRangeSlider::getActiveThumbBoundsInComponent(const juce::Component& targetComponent) const
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

juce::String HorizontalRangeSlider::getActiveThumbTooltipText() const
{
    if (draggingThumb == Lower)
    {
        return juce::String(lowerValue, 2);
    }

    if (draggingThumb == Upper)
    {
        return juce::String(upperValue, 2);
    }

    if (hoveredThumb == HoverLower)
    {
        return juce::String(lowerValue, 2);
    }

    if (hoveredThumb == HoverUpper)
    {
        return juce::String(upperValue, 2);
    }

    return {};
}

void HorizontalRangeSlider::paint(juce::Graphics& graphics)
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    graphics.setColour(AccentGray.brighter(0.02f));
    graphics.fillRoundedRectangle(bounds, roundness);

    juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    graphics.setColour(ThemePink);
    graphics.fillRoundedRectangle(rangeRectangle, roundness);

    juce::Colour upperNormalThumbColour = juce::Colours::white.darker(0.2f).withAlpha(0.35f);
    juce::Colour upperActiveThumbColour = juce::Colours::white.withAlpha(0.5f);

    juce::Colour lowerNormalThumbColour = juce::Colours::white.darker(0.2f).withAlpha(0.35f);
    juce::Colour lowerActiveThumbColour = juce::Colours::white.withAlpha(0.5f);

    juce::Rectangle<float> lowerThumbRectangle = getLowerThumbRectangle();
    juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle();

    bool lowerThumbIsActive = (hoveredThumb == HoverLower || draggingThumb == Lower);
    bool upperThumbIsActive = (hoveredThumb == HoverUpper || draggingThumb == Upper);

    graphics.setColour(lowerThumbIsActive ? lowerActiveThumbColour : lowerNormalThumbColour);
    graphics.fillRoundedRectangle(lowerThumbRectangle, thumbWidth * 0.5f);

    graphics.setColour(upperThumbIsActive ? upperActiveThumbColour : upperNormalThumbColour);
    graphics.fillRoundedRectangle(upperThumbRectangle, thumbWidth * 0.5f);
}

void HorizontalRangeSlider::resized()
{
}

int HorizontalRangeSlider::valueToX(float value) const
{
    juce::Rectangle<int> bounds = getLocalBounds();
    float proportion = (value - minValue) / (maxValue - minValue);

    return juce::roundToInt(
        juce::jmap(
            proportion,
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getRight())
        )
    );
}

float HorizontalRangeSlider::xToValue(int xPosition) const
{
    juce::Rectangle<int> bounds = getLocalBounds();
    float proportion = (static_cast<float>(xPosition - bounds.getX()) / static_cast<float>(bounds.getWidth()));

    return juce::jlimit(minValue, maxValue, minValue + proportion * (maxValue - minValue));
}

float HorizontalRangeSlider::deltaXToValueDelta(float deltaX) const
{
    juce::Rectangle<int> bounds = getLocalBounds();

    if (bounds.getWidth() <= 0)
    {
        return 0.0f;
    }

    float valueRange = maxValue - minValue;
    return (deltaX / static_cast<float>(bounds.getWidth())) * valueRange;
}

juce::Rectangle<float> HorizontalRangeSlider::getRangeRectangle() const
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    float lowerXPosition = static_cast<float>(valueToX(lowerValue));
    float upperXPosition = static_cast<float>(valueToX(upperValue));
    float rangeWidth = juce::jmax(1.0f, upperXPosition - lowerXPosition);

    return juce::Rectangle<float>(
        lowerXPosition,
        bounds.getY(),
        rangeWidth,
        bounds.getHeight()
    );
}

juce::Rectangle<float> HorizontalRangeSlider::getLowerThumbRectangle() const
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    float normalThumbXPosition = rangeRectangle.getX() + thumbSideInset;
    float upperThumbNormalXPosition = rangeRectangle.getRight() - thumbSideInset - thumbWidth;
    float lowerThumbXPosition = normalThumbXPosition;
    float availableSpacing = upperThumbNormalXPosition - normalThumbXPosition;

    if (availableSpacing < visualMinimumThumbSpacing)
    {
        float rangeCentreXPosition = rangeRectangle.getCentreX();
        float halfVisualSpacing = (visualMinimumThumbSpacing + thumbWidth) * 0.5f;

        lowerThumbXPosition = rangeCentreXPosition - halfVisualSpacing;
        lowerThumbXPosition = juce::jlimit(bounds.getX(), bounds.getRight() - thumbWidth, lowerThumbXPosition);
    }

    float thumbYPosition = rangeRectangle.getCentreY() - (thumbHeight * 0.5f);

    return juce::Rectangle<float>(lowerThumbXPosition, thumbYPosition, thumbWidth, thumbHeight);
}

juce::Rectangle<float> HorizontalRangeSlider::getUpperThumbRectangle() const
{
    juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    float lowerThumbNormalXPosition = rangeRectangle.getX() + thumbSideInset;
    float normalThumbXPosition = rangeRectangle.getRight() - thumbSideInset - thumbWidth;
    float upperThumbXPosition = normalThumbXPosition;
    float availableSpacing = normalThumbXPosition - lowerThumbNormalXPosition;

    if (availableSpacing < visualMinimumThumbSpacing)
    {
        float rangeCentreXPosition = rangeRectangle.getCentreX();
        float halfVisualSpacing = (visualMinimumThumbSpacing + thumbWidth) * 0.5f;

        upperThumbXPosition = rangeCentreXPosition + halfVisualSpacing - thumbWidth;
        upperThumbXPosition = juce::jlimit(bounds.getX(), bounds.getRight() - thumbWidth, upperThumbXPosition);
    }

    float thumbYPosition = rangeRectangle.getCentreY() - (thumbHeight * 0.5f);

    return juce::Rectangle<float>(upperThumbXPosition, thumbYPosition, thumbWidth, thumbHeight);
}

juce::Rectangle<float> HorizontalRangeSlider::getActiveThumbRectangle() const
{
    if (draggingThumb == Lower)
    {
        return getLowerThumbRectangle();
    }

    if (draggingThumb == Upper)
    {
        return getUpperThumbRectangle();
    }

    if (hoveredThumb == HoverLower)
    {
        return getLowerThumbRectangle();
    }

    if (hoveredThumb == HoverUpper)
    {
        return getUpperThumbRectangle();
    }

    return {};
}

HorizontalRangeSlider::HoveredThumb HorizontalRangeSlider::getHoveredThumbAtPosition(juce::Point<int> mousePosition) const
{
    juce::Rectangle<float> lowerThumbRectangle = getLowerThumbRectangle().expanded(6.0f, 6.0f);
    juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle().expanded(6.0f, 6.0f);

    if (lowerThumbRectangle.contains(mousePosition.toFloat()))
    {
        return HoverLower;
    }

    if (upperThumbRectangle.contains(mousePosition.toFloat()))
    {
        return HoverUpper;
    }

    return HoverNone;
}

float HorizontalRangeSlider::snapValueToStep(float value) const
{
    if (!steppingEnabled || stepSize <= 0.0f)
    {
        return value;
    }

    float stepsFromMinimum = std::round((value - minValue) / stepSize);
    float snappedValue = minValue + (stepsFromMinimum * stepSize);

    return juce::jlimit(minValue, maxValue, snappedValue);
}

void HorizontalRangeSlider::mouseDown(const juce::MouseEvent& mouseEvent)
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

    dragStartMouseX = mouseEvent.getPosition().getX();
    dragStartLowerValue = lowerValue;
    dragStartUpperValue = upperValue;

    repaint();
    notifyTooltipStateChanged();
}

void HorizontalRangeSlider::mouseDrag(const juce::MouseEvent& mouseEvent)
{
    float mouseDeltaX = static_cast<float>(mouseEvent.getPosition().getX() - dragStartMouseX);
    float valueDelta = deltaXToValueDelta(mouseDeltaX);

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

void HorizontalRangeSlider::mouseMove(const juce::MouseEvent& mouseEvent)
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
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
}

void HorizontalRangeSlider::mouseExit(const juce::MouseEvent& mouseEvent)
{
    juce::ignoreUnused(mouseEvent);

    hoveredThumb = HoverNone;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
    notifyTooltipStateChanged();
}

void HorizontalRangeSlider::mouseUp(const juce::MouseEvent& mouseEvent)
{
    juce::ignoreUnused(mouseEvent);

    draggingThumb = None;
    repaint();
    notifyTooltipStateChanged();
}

void HorizontalRangeSlider::notifyTooltipStateChanged()
{
    if (OnTooltipStateChanged)
    {
        OnTooltipStateChanged();
    }
}

HorizontalRangeSliderAttachment::HorizontalRangeSliderAttachment(
    juce::AudioProcessorValueTreeState& parameterValueTreeState,
    const juce::String& lowerParameterID,
    const juce::String& upperParameterID,
    HorizontalRangeSlider& rangeSliderToControl)
    : valueTreeState(parameterValueTreeState),
      lowerID(lowerParameterID),
      upperID(upperParameterID),
      rangeSlider(rangeSliderToControl)
{
    valueTreeState.addParameterListener(lowerID, this);
    valueTreeState.addParameterListener(upperID, this);

    updateSliderFromParameters();

    rangeSlider.OnLowerValueChanged = [this](float newValue)
    {
        if (!updatingParameter)
        {
            updatingSlider = true;

            if (auto* parameter = valueTreeState.getParameter(lowerID))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(parameter->convertTo0to1(newValue));
                parameter->endChangeGesture();
            }

            updatingSlider = false;
        }
    };

    rangeSlider.OnUpperValueChanged = [this](float newValue)
    {
        if (!updatingParameter)
        {
            updatingSlider = true;

            if (auto* parameter = valueTreeState.getParameter(upperID))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(parameter->convertTo0to1(newValue));
                parameter->endChangeGesture();
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

void HorizontalRangeSliderAttachment::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(parameterID, newValue);

    if (!updatingSlider)
    {
        updatingParameter = true;
        juce::MessageManager::callAsync([this]()
        {
            updateSliderFromParameters();
            updatingParameter = false;
        });
    }
}

void HorizontalRangeSliderAttachment::updateSliderFromParameters()
{
    if (auto* lowerParameter = valueTreeState.getParameter(lowerID))
    {
        float lowerParameterValue = lowerParameter->convertFrom0to1(lowerParameter->getValue());
        rangeSlider.setLowerValue(lowerParameterValue);
    }

    if (auto* upperParameter = valueTreeState.getParameter(upperID))
    {
        float upperParameterValue = upperParameter->convertFrom0to1(upperParameter->getValue());
        rangeSlider.setUpperValue(upperParameterValue);
    }
}