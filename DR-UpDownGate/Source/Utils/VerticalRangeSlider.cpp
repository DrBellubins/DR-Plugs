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

void VerticalRangeSlider::paint(juce::Graphics& Graphics)
{
    juce::Rectangle Bounds = getLocalBounds().toFloat();

    Graphics.setColour(AccentGray.brighter(0.02f));
    Graphics.fillRoundedRectangle(Bounds, roundness);

    juce::Path BackgroundPath;
    BackgroundPath.addRoundedRectangle(Bounds, roundness);

    Graphics.saveState();
    Graphics.reduceClipRegion(BackgroundPath);

    constexpr float InnerShadowSize = 10.0f;
    constexpr float HighlightSize = 20.0f;

    juce::ColourGradient TopShadowGradient(
        juce::Colours::black.withAlpha(0.28f),
        Bounds.getCentreX(),
        Bounds.getY(),
        juce::Colours::transparentBlack,
        Bounds.getCentreX(),
        Bounds.getY() + InnerShadowSize,
        false
    );

    Graphics.setGradientFill(TopShadowGradient);
    Graphics.fillRect(
        Bounds.getX(),
        Bounds.getY(),
        Bounds.getWidth(),
        InnerShadowSize
    );

    juce::ColourGradient BottomShadowGradient(
        juce::Colours::transparentBlack,
        Bounds.getCentreX(),
        Bounds.getBottom() - InnerShadowSize,
        juce::Colours::black.withAlpha(0.20f),
        Bounds.getCentreX(),
        Bounds.getBottom(),
        false
    );

    Graphics.setGradientFill(BottomShadowGradient);
    Graphics.fillRect(
        Bounds.getX(),
        Bounds.getBottom() - InnerShadowSize,
        Bounds.getWidth(),
        InnerShadowSize
    );

    juce::ColourGradient LeftShadowGradient(
        juce::Colours::black.withAlpha(0.12f),
        Bounds.getX(),
        Bounds.getCentreY(),
        juce::Colours::transparentBlack,
        Bounds.getX() + InnerShadowSize,
        Bounds.getCentreY(),
        false
    );

    Graphics.setGradientFill(LeftShadowGradient);
    Graphics.fillRect(
        Bounds.getX(),
        Bounds.getY(),
        InnerShadowSize,
        Bounds.getHeight()
    );

    juce::ColourGradient RightHighlightGradient(
        juce::Colours::transparentWhite,
        Bounds.getRight() - HighlightSize,
        Bounds.getCentreY(),
        juce::Colours::white.withAlpha(0.05f),
        Bounds.getRight(),
        Bounds.getCentreY(),
        false
    );

    Graphics.setGradientFill(RightHighlightGradient);
    Graphics.fillRect(
        Bounds.getRight() - HighlightSize,
        Bounds.getY(),
        HighlightSize,
        Bounds.getHeight()
    );

    Graphics.restoreState();

    juce::Rectangle RangeRectangle = getRangeRectangle();
    juce::Path RangePath;
    RangePath.addRoundedRectangle(RangeRectangle, roundness);

    Graphics.saveState();
    Graphics.reduceClipRegion(RangePath);

    juce::ColourGradient RangeFaceGradient(
    ThemePink,
        Bounds.getCentreX(),
        Bounds.getY(),
    ThemePink.darker(1.0f),
        Bounds.getCentreX(),
        Bounds.getBottom(),
        false
    );

    Graphics.setGradientFill(RangeFaceGradient);
    Graphics.fillRoundedRectangle(RangeRectangle, roundness);

    float BevelSize = juce::jmin(12.0f, RangeRectangle.getHeight() * 0.5f);
    BevelSize = juce::jmax(3.0f, BevelSize);

    juce::ColourGradient TopHighlightGradient(
        juce::Colours::white.withAlpha(0.30f),
        RangeRectangle.getCentreX(),
        RangeRectangle.getY(),
        juce::Colours::transparentWhite,
        RangeRectangle.getCentreX(),
        RangeRectangle.getY() + BevelSize,
        false
    );

    Graphics.setGradientFill(TopHighlightGradient);
    Graphics.fillRect(
        RangeRectangle.getX(),
        RangeRectangle.getY(),
        RangeRectangle.getWidth(),
        BevelSize
    );

    juce::ColourGradient LeftHighlightGradient(
        juce::Colours::white.withAlpha(0.16f),
        RangeRectangle.getX(),
        RangeRectangle.getCentreY(),
        juce::Colours::transparentWhite,
        RangeRectangle.getX() + BevelSize,
        RangeRectangle.getCentreY(),
        false
    );

    Graphics.setGradientFill(LeftHighlightGradient);
    Graphics.fillRect(
        RangeRectangle.getX(),
        RangeRectangle.getY(),
        BevelSize,
        RangeRectangle.getHeight()
    );

    juce::ColourGradient BottomShadowGradientRange(
        juce::Colours::transparentBlack,
        RangeRectangle.getCentreX(),
        RangeRectangle.getBottom() - BevelSize,
        juce::Colours::black.withAlpha(0.22f),
        RangeRectangle.getCentreX(),
        RangeRectangle.getBottom(),
        false
    );

    Graphics.setGradientFill(BottomShadowGradientRange);
    Graphics.fillRect(
        RangeRectangle.getX(),
        RangeRectangle.getBottom() - BevelSize,
        RangeRectangle.getWidth(),
        BevelSize
    );

    juce::ColourGradient RightShadowGradientRange(
        juce::Colours::transparentBlack,
        RangeRectangle.getRight() - BevelSize,
        RangeRectangle.getCentreY(),
        juce::Colours::black.withAlpha(0.16f),
        RangeRectangle.getRight(),
        RangeRectangle.getCentreY(),
        false
    );

    Graphics.setGradientFill(RightShadowGradientRange);
    Graphics.fillRect(
        RangeRectangle.getRight() - BevelSize,
        RangeRectangle.getY(),
        BevelSize,
        RangeRectangle.getHeight()
    );

    Graphics.restoreState();

    juce::Colour UpperNormalThumbColour = juce::Colours::white.darker(0.2f).withAlpha(0.35f);
    juce::Colour UpperActiveThumbColour = juce::Colours::white.withAlpha(0.5f);

    juce::Colour LowerNormalThumbColour = juce::Colours::white.darker(0.2f).withAlpha(0.35f);
    juce::Colour LowerActiveThumbColour = juce::Colours::white.withAlpha(0.5f);

    juce::Rectangle UpperThumbRectangle = getUpperThumbRectangle();
    juce::Rectangle LowerThumbRectangle = getLowerThumbRectangle();

    bool UpperThumbIsActive = (hoveredThumb == HoverUpper || draggingThumb == Upper);
    bool LowerThumbIsActive = (hoveredThumb == HoverLower || draggingThumb == Lower);

    Graphics.setColour(UpperThumbIsActive ? UpperActiveThumbColour : UpperNormalThumbColour);
    Graphics.fillRoundedRectangle(UpperThumbRectangle, thumbHeight * 0.5f);

    Graphics.setColour(LowerThumbIsActive ? LowerActiveThumbColour : LowerNormalThumbColour);
    Graphics.fillRoundedRectangle(LowerThumbRectangle, thumbHeight * 0.5f);
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