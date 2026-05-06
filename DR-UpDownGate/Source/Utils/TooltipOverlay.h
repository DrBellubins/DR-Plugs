#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VerticalRangeSlider.h"
#include "Theme.h"

class TooltipOverlay : public juce::Component,
                       private juce::Timer
{
public:
    explicit TooltipOverlay(VerticalRangeSlider& verticalRangeSlider)
        : trackedSlider(verticalRangeSlider)
    {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(true);
        startTimerHz(60);
    }

    void paint(juce::Graphics& graphics) override
    {
        if (currentAlpha <= 0.001f)
        {
            return;
        }

        if (cachedThumbBounds.isEmpty() || cachedTooltipText.isEmpty() || cachedActiveThumb == VerticalRangeSlider::NoThumb)
        {
            return;
        }

        constexpr float tooltipHeight = 24.0f;
        constexpr float tooltipHorizontalPadding = 8.0f;
        constexpr float tooltipVerticalGap = 10.0f;
        constexpr float tooltipCornerRadius = 6.0f;

        juce::Font tooltipFont(14.0f);
        graphics.setFont(tooltipFont);

        float tooltipWidth = static_cast<float>(tooltipFont.getStringWidth(cachedTooltipText))
                             + (tooltipHorizontalPadding * 2.0f);

        float tooltipXPosition = cachedThumbBounds.getCentreX() - (tooltipWidth * 0.5f);
        float tooltipYPosition = 0.0f;

        if (cachedActiveThumb == VerticalRangeSlider::UpperThumb)
        {
            tooltipYPosition = cachedThumbBounds.getY() - tooltipVerticalGap - tooltipHeight;
        }
        else
        {
            tooltipYPosition = cachedThumbBounds.getBottom() + tooltipVerticalGap;
        }

        juce::Rectangle<float> tooltipBounds(
            tooltipXPosition,
            tooltipYPosition,
            tooltipWidth,
            tooltipHeight
        );

        juce::Rectangle<float> overlayBounds = getLocalBounds().toFloat();

        if (tooltipBounds.getX() < overlayBounds.getX())
        {
            tooltipBounds.setX(overlayBounds.getX());
        }

        if (tooltipBounds.getRight() > overlayBounds.getRight())
        {
            tooltipBounds.setX(overlayBounds.getRight() - tooltipWidth);
        }

        if (tooltipBounds.getY() < overlayBounds.getY())
        {
            tooltipBounds.setY(overlayBounds.getY());
        }

        if (tooltipBounds.getBottom() > overlayBounds.getBottom())
        {
            tooltipBounds.setY(overlayBounds.getBottom() - tooltipHeight);
        }

        juce::Colour backgroundColour = AccentGray.darker(0.25f).withAlpha(currentAlpha);
        juce::Colour textColour = juce::Colours::white.withAlpha(currentAlpha);

        graphics.setColour(backgroundColour);
        graphics.fillRoundedRectangle(tooltipBounds, tooltipCornerRadius);

        graphics.setColour(textColour);
        graphics.drawFittedText(
            cachedTooltipText,
            tooltipBounds.getSmallestIntegerContainer(),
            juce::Justification::centred,
            1
        );
    }

private:
    void timerCallback() override
    {
        bool shouldShowTooltip = trackedSlider.shouldShowTooltip();

        if (shouldShowTooltip)
        {
            juce::Rectangle<float> latestThumbBounds = trackedSlider.getActiveThumbBoundsInComponent(*this);
            juce::String latestTooltipText = trackedSlider.getActiveThumbTooltipText();
            VerticalRangeSlider::ActiveThumb latestActiveThumb = trackedSlider.getActiveThumb();

            if (!latestThumbBounds.isEmpty()
                && !latestTooltipText.isEmpty()
                && latestActiveThumb != VerticalRangeSlider::NoThumb)
            {
                cachedThumbBounds = latestThumbBounds;
                cachedTooltipText = latestTooltipText;
                cachedActiveThumb = latestActiveThumb;
            }
        }

        float targetAlpha = shouldShowTooltip ? 1.0f : 0.0f;
        float fadeInSpeed = 0.07f;
        float fadeOutSpeed = 0.045f;

        if (currentAlpha < targetAlpha)
        {
            currentAlpha = juce::jmin(targetAlpha, currentAlpha + fadeInSpeed);
            repaint();
        }
        else if (currentAlpha > targetAlpha)
        {
            currentAlpha = juce::jmax(targetAlpha, currentAlpha - fadeOutSpeed);
            repaint();

            if (currentAlpha <= 0.001f)
            {
                cachedThumbBounds = {};
                cachedTooltipText.clear();
                cachedActiveThumb = VerticalRangeSlider::NoThumb;
            }
        }
    }

    VerticalRangeSlider& trackedSlider;

    float currentAlpha = 0.0f;
    juce::Rectangle<float> cachedThumbBounds;
    juce::String cachedTooltipText;
    VerticalRangeSlider::ActiveThumb cachedActiveThumb = VerticalRangeSlider::NoThumb;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TooltipOverlay)
};