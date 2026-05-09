#include "HorizontalRangeSlider.h"

HorizontalRangeSlider::HorizontalRangeSlider(float MinimumValue, float MaximumValue)
    : minValue(MinimumValue),
      maxValue(MaximumValue),
      lowerValue(MinimumValue),
      upperValue(MaximumValue)
{
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void HorizontalRangeSlider::setLowerValue(float NewValue)
{
    const float snappedValue = snapValueToStep(NewValue);
    const float maximumLowerValue = upperValue - minimumRange;
    const float clampedValue = juce::jlimit(minValue, maximumLowerValue, snappedValue);

    if (std::abs(lowerValue - clampedValue) < 0.000001f)
    {
        return;
    }

    lowerValue = clampedValue;

    if (OnLowerValueChanged)
    {
        OnLowerValueChanged(lowerValue);
    }

    repaint();
    notifyTooltipStateChanged();
}

void HorizontalRangeSlider::setUpperValue(float NewValue)
{
    const float snappedValue = snapValueToStep(NewValue);
    const float minimumUpperValue = lowerValue + minimumRange;
    const float clampedValue = juce::jlimit(minimumUpperValue, maxValue, snappedValue);

    if (std::abs(upperValue - clampedValue) < 0.000001f)
    {
        return;
    }

    upperValue = clampedValue;

    if (OnUpperValueChanged)
    {
        OnUpperValueChanged(upperValue);
    }

    repaint();
    notifyTooltipStateChanged();
}

void HorizontalRangeSlider::setRoundness(float NewRadius)
{
    roundness = juce::jmax(0.0f, NewRadius);
    repaint();
}

void HorizontalRangeSlider::setMinimumRange(float NewMinimumRange)
{
    minimumRange = juce::jmax(0.0f, NewMinimumRange);

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

void HorizontalRangeSlider::setSteppingEnabled(bool ShouldEnableStepping)
{
    steppingEnabled = ShouldEnableStepping;

    if (steppingEnabled)
    {
        setLowerValue(lowerValue);
        setUpperValue(upperValue);
    }
}

void HorizontalRangeSlider::setStepSize(float NewStepSize)
{
    stepSize = juce::jmax(0.000001f, NewStepSize);

    if (steppingEnabled)
    {
        setLowerValue(lowerValue);
        setUpperValue(upperValue);
    }
}

bool HorizontalRangeSlider::ShouldShowTooltip() const
{
    return draggingThumb != None || hoveredThumb != HoverNone;
}

juce::Rectangle<float> HorizontalRangeSlider::GetTooltipTargetBoundsInComponent(
    const juce::Component& TargetComponent) const
{
    const juce::Rectangle<float> activeThumbRectangle = getActiveThumbRectangle();

    if (activeThumbRectangle.isEmpty())
    {
        return {};
    }

    const juce::Rectangle<int> integerThumbRectangle =
        activeThumbRectangle.getSmallestIntegerContainer();

    const juce::Rectangle<int> targetRectangle =
        TargetComponent.getLocalArea(this, integerThumbRectangle);

    return targetRectangle.toFloat();
}

juce::String HorizontalRangeSlider::GetTooltipText() const
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

TooltipClient::Placement HorizontalRangeSlider::GetTooltipPlacement() const
{
    return TooltipClient::Placement::Above;
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

void HorizontalRangeSlider::paint(juce::Graphics& GraphicsContext)
{
    const juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    GraphicsContext.setColour(AccentGray.brighter(0.02f));
    GraphicsContext.fillRoundedRectangle(bounds, roundness);

    const juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    GraphicsContext.setColour(ThemePink);
    GraphicsContext.fillRoundedRectangle(rangeRectangle, roundness);

    const juce::Colour upperNormalThumbColour = juce::Colours::white.darker(0.2f).withAlpha(0.35f);
    const juce::Colour upperActiveThumbColour = juce::Colours::white.withAlpha(0.5f);

    const juce::Colour lowerNormalThumbColour = juce::Colours::white.darker(0.2f).withAlpha(0.35f);
    const juce::Colour lowerActiveThumbColour = juce::Colours::white.withAlpha(0.5f);

    const juce::Rectangle<float> lowerThumbRectangle = getLowerThumbRectangle();
    const juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle();

    const bool lowerThumbIsActive = (hoveredThumb == HoverLower || draggingThumb == Lower);
    const bool upperThumbIsActive = (hoveredThumb == HoverUpper || draggingThumb == Upper);

    GraphicsContext.setColour(lowerThumbIsActive ? lowerActiveThumbColour : lowerNormalThumbColour);
    GraphicsContext.fillRoundedRectangle(lowerThumbRectangle, thumbWidth * 0.5f);

    GraphicsContext.setColour(upperThumbIsActive ? upperActiveThumbColour : upperNormalThumbColour);
    GraphicsContext.fillRoundedRectangle(upperThumbRectangle, thumbWidth * 0.5f);
}

void HorizontalRangeSlider::resized()
{
}

int HorizontalRangeSlider::valueToX(float Value) const
{
    const juce::Rectangle<int> bounds = getLocalBounds();
    const float proportion = (Value - minValue) / (maxValue - minValue);

    return juce::roundToInt(
        juce::jmap(
            proportion,
            static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getRight())));
}

float HorizontalRangeSlider::xToValue(int XPosition) const
{
    const juce::Rectangle<int> bounds = getLocalBounds();

    if (bounds.getWidth() <= 0)
    {
        return minValue;
    }

    const float proportion =
        static_cast<float>(XPosition - bounds.getX()) / static_cast<float>(bounds.getWidth());

    return juce::jlimit(
        minValue,
        maxValue,
        minValue + proportion * (maxValue - minValue));
}

float HorizontalRangeSlider::deltaXToValueDelta(float DeltaX) const
{
    const juce::Rectangle<int> bounds = getLocalBounds();

    if (bounds.getWidth() <= 0)
    {
        return 0.0f;
    }

    const float valueRange = maxValue - minValue;
    return (DeltaX / static_cast<float>(bounds.getWidth())) * valueRange;
}

juce::Rectangle<float> HorizontalRangeSlider::getRangeRectangle() const
{
    const juce::Rectangle<float> bounds = getLocalBounds().toFloat();

    const float lowerXPosition = static_cast<float>(valueToX(lowerValue));
    const float upperXPosition = static_cast<float>(valueToX(upperValue));
    const float rangeWidth = juce::jmax(1.0f, upperXPosition - lowerXPosition);

    return juce::Rectangle<float>(
        lowerXPosition,
        bounds.getY(),
        rangeWidth,
        bounds.getHeight());
}

juce::Rectangle<float> HorizontalRangeSlider::getLowerThumbRectangle() const
{
    const juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    const juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    const float normalThumbXPosition = rangeRectangle.getX() + thumbSideInset;
    const float upperThumbNormalXPosition = rangeRectangle.getRight() - thumbSideInset - thumbWidth;

    float lowerThumbXPosition = normalThumbXPosition;
    const float availableSpacing = upperThumbNormalXPosition - normalThumbXPosition;

    if (availableSpacing < visualMinimumThumbSpacing)
    {
        const float rangeCentreXPosition = rangeRectangle.getCentreX();
        const float halfVisualSpacing = (visualMinimumThumbSpacing + thumbWidth) * 0.5f;

        lowerThumbXPosition = rangeCentreXPosition - halfVisualSpacing;
        lowerThumbXPosition = juce::jlimit(bounds.getX(), bounds.getRight() - thumbWidth, lowerThumbXPosition);
    }

    const float thumbYPosition = rangeRectangle.getCentreY() - (thumbHeight * 0.5f);

    return juce::Rectangle<float>(
        lowerThumbXPosition,
        thumbYPosition,
        thumbWidth,
        thumbHeight);
}

juce::Rectangle<float> HorizontalRangeSlider::getUpperThumbRectangle() const
{
    const juce::Rectangle<float> bounds = getLocalBounds().toFloat();
    const juce::Rectangle<float> rangeRectangle = getRangeRectangle();

    const float lowerThumbNormalXPosition = rangeRectangle.getX() + thumbSideInset;
    const float normalThumbXPosition = rangeRectangle.getRight() - thumbSideInset - thumbWidth;

    float upperThumbXPosition = normalThumbXPosition;
    const float availableSpacing = normalThumbXPosition - lowerThumbNormalXPosition;

    if (availableSpacing < visualMinimumThumbSpacing)
    {
        const float rangeCentreXPosition = rangeRectangle.getCentreX();
        const float halfVisualSpacing = (visualMinimumThumbSpacing + thumbWidth) * 0.5f;

        upperThumbXPosition = rangeCentreXPosition + halfVisualSpacing - thumbWidth;
        upperThumbXPosition = juce::jlimit(bounds.getX(), bounds.getRight() - thumbWidth, upperThumbXPosition);
    }

    const float thumbYPosition = rangeRectangle.getCentreY() - (thumbHeight * 0.5f);

    return juce::Rectangle<float>(
        upperThumbXPosition,
        thumbYPosition,
        thumbWidth,
        thumbHeight);
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

HorizontalRangeSlider::HoveredThumb HorizontalRangeSlider::getHoveredThumbAtPosition(
    juce::Point<int> MousePosition) const
{
    const juce::Rectangle<float> lowerThumbRectangle = getLowerThumbRectangle().expanded(6.0f, 6.0f);
    const juce::Rectangle<float> upperThumbRectangle = getUpperThumbRectangle().expanded(6.0f, 6.0f);

    if (lowerThumbRectangle.contains(MousePosition.toFloat()))
    {
        return HoverLower;
    }

    if (upperThumbRectangle.contains(MousePosition.toFloat()))
    {
        return HoverUpper;
    }

    return HoverNone;
}

float HorizontalRangeSlider::snapValueToStep(float Value) const
{
    if (!steppingEnabled || stepSize <= 0.0f)
    {
        return Value;
    }

    const float stepsFromMinimum = std::round((Value - minValue) / stepSize);
    const float snappedValue = minValue + (stepsFromMinimum * stepSize);

    return juce::jlimit(minValue, maxValue, snappedValue);
}

void HorizontalRangeSlider::mouseDown(const juce::MouseEvent& MouseEvent)
{
    hoveredThumb = getHoveredThumbAtPosition(MouseEvent.getPosition());

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

    dragStartMouseX = MouseEvent.getPosition().getX();
    dragStartLowerValue = lowerValue;
    dragStartUpperValue = upperValue;

    if (draggingThumb != None && OnDragStarted)
    {
        OnDragStarted();
    }

    repaint();
    notifyTooltipStateChanged();
}

void HorizontalRangeSlider::mouseDrag(const juce::MouseEvent& MouseEvent)
{
    const float mouseDeltaX =
        static_cast<float>(MouseEvent.getPosition().getX() - dragStartMouseX);

    const float valueDelta = deltaXToValueDelta(mouseDeltaX);

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

void HorizontalRangeSlider::mouseMove(const juce::MouseEvent& MouseEvent)
{
    const HoveredThumb newHoveredThumb = getHoveredThumbAtPosition(MouseEvent.getPosition());

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

void HorizontalRangeSlider::mouseExit(const juce::MouseEvent& MouseEvent)
{
    juce::ignoreUnused(MouseEvent);

    hoveredThumb = HoverNone;
    setMouseCursor(juce::MouseCursor::NormalCursor);
    repaint();
    notifyTooltipStateChanged();
}

void HorizontalRangeSlider::mouseUp(const juce::MouseEvent& MouseEvent)
{
    juce::ignoreUnused(MouseEvent);

    const bool wasDragging = (draggingThumb != None);
    draggingThumb = None;

    if (wasDragging && OnDragEnded)
    {
        OnDragEnded();
    }

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
    juce::AudioProcessorValueTreeState& ParameterValueTreeState,
    const juce::String& LowerParameterID,
    const juce::String& UpperParameterID,
    HorizontalRangeSlider& RangeSlider)
    : valueTreeState(ParameterValueTreeState),
      lowerID(LowerParameterID),
      upperID(UpperParameterID),
      rangeSlider(RangeSlider)
{
    lowerParameter = valueTreeState.getParameter(lowerID);
    upperParameter = valueTreeState.getParameter(upperID);

    jassert(lowerParameter != nullptr);
    jassert(upperParameter != nullptr);

    valueTreeState.addParameterListener(lowerID, this);
    valueTreeState.addParameterListener(upperID, this);

    updateSliderFromParameters();

    rangeSlider.OnLowerValueChanged = [this](float NewValue)
    {
        if (updatingParameter || lowerParameter == nullptr)
        {
            return;
        }

        updatingSlider = true;
        lowerParameter->setValueNotifyingHost(lowerParameter->convertTo0to1(NewValue));
        updatingSlider = false;
    };

    rangeSlider.OnUpperValueChanged = [this](float NewValue)
    {
        if (updatingParameter || upperParameter == nullptr)
        {
            return;
        }

        updatingSlider = true;
        upperParameter->setValueNotifyingHost(upperParameter->convertTo0to1(NewValue));
        updatingSlider = false;
    };

    rangeSlider.OnDragStarted = [this]()
    {
        beginGesture();
    };

    rangeSlider.OnDragEnded = [this]()
    {
        endGesture();
    };
}

HorizontalRangeSliderAttachment::~HorizontalRangeSliderAttachment()
{
    endGesture();

    valueTreeState.removeParameterListener(lowerID, this);
    valueTreeState.removeParameterListener(upperID, this);

    rangeSlider.OnLowerValueChanged = nullptr;
    rangeSlider.OnUpperValueChanged = nullptr;
    rangeSlider.OnDragStarted = nullptr;
    rangeSlider.OnDragEnded = nullptr;
}

void HorizontalRangeSliderAttachment::parameterChanged(
    const juce::String& ParameterID,
    float NewValue)
{
    juce::ignoreUnused(ParameterID, NewValue);

    if (updatingSlider)
    {
        return;
    }

    updatingParameter = true;

    juce::MessageManager::callAsync([this]()
    {
        updateSliderFromParameters();
        updatingParameter = false;
    });
}

void HorizontalRangeSliderAttachment::beginGesture()
{
    if (gestureInProgress)
    {
        return;
    }

    if (lowerParameter != nullptr)
    {
        lowerParameter->beginChangeGesture();
    }

    if (upperParameter != nullptr)
    {
        upperParameter->beginChangeGesture();
    }

    gestureInProgress = true;
}

void HorizontalRangeSliderAttachment::endGesture()
{
    if (!gestureInProgress)
    {
        return;
    }

    if (lowerParameter != nullptr)
    {
        lowerParameter->endChangeGesture();
    }

    if (upperParameter != nullptr)
    {
        upperParameter->endChangeGesture();
    }

    gestureInProgress = false;
}

void HorizontalRangeSliderAttachment::updateSliderFromParameters()
{
    if (lowerParameter != nullptr)
    {
        const float lowerParameterValue =
            lowerParameter->convertFrom0to1(lowerParameter->getValue());

        rangeSlider.setLowerValue(lowerParameterValue);
    }

    if (upperParameter != nullptr)
    {
        const float upperParameterValue =
            upperParameter->convertFrom0to1(upperParameter->getValue());

        rangeSlider.setUpperValue(upperParameterValue);
    }
}