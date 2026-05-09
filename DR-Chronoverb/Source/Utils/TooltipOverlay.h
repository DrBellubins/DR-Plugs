#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

class TooltipClient
{
public:
    enum class Placement
    {
        Above,
        Below,
        Left,
        Right
    };

    virtual ~TooltipClient() = default;

    virtual bool ShouldShowTooltip() const = 0;

    virtual juce::Rectangle<float> GetTooltipTargetBoundsInComponent(
        const juce::Component& TargetComponent) const = 0;

    virtual juce::String GetTooltipText() const = 0;

    virtual Placement GetTooltipPlacement() const
    {
        return Placement::Above;
    }
};

class TooltipOverlay : public juce::Component,
                       private juce::Timer
{
public:
    explicit TooltipOverlay(TooltipClient& TooltipClientReference)
        : trackedTooltipClient(TooltipClientReference)
    {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(true);
        startTimerHz(60);
    }

    ~TooltipOverlay() override = default;

    void paint(juce::Graphics& GraphicsContext) override
    {
        if (currentAlpha <= 0.001f)
        {
            return;
        }

        if (cachedTargetBounds.isEmpty() || cachedTooltipText.isEmpty())
        {
            return;
        }

        constexpr float tooltipHeight = 24.0f;
        constexpr float tooltipHorizontalPadding = 8.0f;
        constexpr float tooltipGap = 10.0f;
        constexpr float tooltipCornerRadius = 6.0f;

        juce::Font tooltipFont(14.0f);
        GraphicsContext.setFont(tooltipFont);

        const float tooltipWidth =
            static_cast<float>(tooltipFont.getStringWidth(cachedTooltipText))
            + (tooltipHorizontalPadding * 2.0f);

        juce::Rectangle<float> tooltipBounds = CalculateTooltipBounds(
            tooltipWidth,
            tooltipHeight,
            tooltipGap);

        const juce::Rectangle<float> overlayBounds = getLocalBounds().toFloat();
        ClampTooltipBoundsToOverlay(tooltipBounds, overlayBounds);

        const juce::Colour backgroundColour = juce::Colours::black.withAlpha(currentAlpha);
        const juce::Colour textColour = juce::Colours::white.withAlpha(currentAlpha);

        GraphicsContext.setColour(backgroundColour);
        GraphicsContext.fillRoundedRectangle(tooltipBounds, tooltipCornerRadius);

        GraphicsContext.setColour(textColour);
        GraphicsContext.drawFittedText(
            cachedTooltipText,
            tooltipBounds.getSmallestIntegerContainer(),
            juce::Justification::centred,
            1);
    }

private:
    void timerCallback() override
    {
        const bool shouldShowTooltip = trackedTooltipClient.ShouldShowTooltip();
        bool shouldRepaint = false;

        if (shouldShowTooltip)
        {
            const juce::Rectangle<float> latestTargetBounds =
                trackedTooltipClient.GetTooltipTargetBoundsInComponent(*this);

            const juce::String latestTooltipText =
                trackedTooltipClient.GetTooltipText();

            const TooltipClient::Placement latestPlacement =
                trackedTooltipClient.GetTooltipPlacement();

            if (!latestTargetBounds.isEmpty() && !latestTooltipText.isEmpty())
            {
                if (cachedTargetBounds != latestTargetBounds
                    || cachedTooltipText != latestTooltipText
                    || cachedPlacement != latestPlacement)
                {
                    cachedTargetBounds = latestTargetBounds;
                    cachedTooltipText = latestTooltipText;
                    cachedPlacement = latestPlacement;
                    shouldRepaint = true;
                }
            }
        }

        const float targetAlpha = shouldShowTooltip ? 1.0f : 0.0f;
        const float fadeInSpeed = 0.07f;
        const float fadeOutSpeed = 0.045f;

        if (currentAlpha < targetAlpha)
        {
            currentAlpha = juce::jmin(targetAlpha, currentAlpha + fadeInSpeed);
            shouldRepaint = true;
        }
        else if (currentAlpha > targetAlpha)
        {
            currentAlpha = juce::jmax(targetAlpha, currentAlpha - fadeOutSpeed);
            shouldRepaint = true;

            if (currentAlpha <= 0.001f)
            {
                cachedTargetBounds = {};
                cachedTooltipText.clear();
                cachedPlacement = TooltipClient::Placement::Above;
            }
        }

        if (shouldRepaint)
        {
            repaint();
        }
    }

    juce::Rectangle<float> CalculateTooltipBounds(
        float TooltipWidth,
        float TooltipHeight,
        float TooltipGap) const
    {
        float tooltipXPosition = 0.0f;
        float tooltipYPosition = 0.0f;

        switch (cachedPlacement)
        {
            case TooltipClient::Placement::Above:
            {
                tooltipXPosition = cachedTargetBounds.getCentreX() - (TooltipWidth * 0.5f);
                tooltipYPosition = cachedTargetBounds.getY() - TooltipGap - TooltipHeight;
                break;
            }

            case TooltipClient::Placement::Below:
            {
                tooltipXPosition = cachedTargetBounds.getCentreX() - (TooltipWidth * 0.5f);
                tooltipYPosition = cachedTargetBounds.getBottom() + TooltipGap;
                break;
            }

            case TooltipClient::Placement::Left:
            {
                tooltipXPosition = cachedTargetBounds.getX() - TooltipGap - TooltipWidth;
                tooltipYPosition = cachedTargetBounds.getCentreY() - (TooltipHeight * 0.5f);
                break;
            }

            case TooltipClient::Placement::Right:
            {
                tooltipXPosition = cachedTargetBounds.getRight() + TooltipGap;
                tooltipYPosition = cachedTargetBounds.getCentreY() - (TooltipHeight * 0.5f);
                break;
            }
        }

        return juce::Rectangle<float>(
            tooltipXPosition,
            tooltipYPosition,
            TooltipWidth,
            TooltipHeight);
    }

    void ClampTooltipBoundsToOverlay(
        juce::Rectangle<float>& TooltipBounds,
        const juce::Rectangle<float>& OverlayBounds) const
    {
        if (TooltipBounds.getWidth() > OverlayBounds.getWidth())
        {
            TooltipBounds.setWidth(OverlayBounds.getWidth());
            TooltipBounds.setX(OverlayBounds.getX());
        }
        else
        {
            if (TooltipBounds.getX() < OverlayBounds.getX())
            {
                TooltipBounds.setX(OverlayBounds.getX());
            }

            if (TooltipBounds.getRight() > OverlayBounds.getRight())
            {
                TooltipBounds.setX(OverlayBounds.getRight() - TooltipBounds.getWidth());
            }
        }

        if (TooltipBounds.getHeight() > OverlayBounds.getHeight())
        {
            TooltipBounds.setHeight(OverlayBounds.getHeight());
            TooltipBounds.setY(OverlayBounds.getY());
        }
        else
        {
            if (TooltipBounds.getY() < OverlayBounds.getY())
            {
                TooltipBounds.setY(OverlayBounds.getY());
            }

            if (TooltipBounds.getBottom() > OverlayBounds.getBottom())
            {
                TooltipBounds.setY(OverlayBounds.getBottom() - TooltipBounds.getHeight());
            }
        }
    }

    TooltipClient& trackedTooltipClient;

    float currentAlpha = 0.0f;
    juce::Rectangle<float> cachedTargetBounds;
    juce::String cachedTooltipText;
    TooltipClient::Placement cachedPlacement = TooltipClient::Placement::Above;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TooltipOverlay)
};