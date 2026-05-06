#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VerticalRangeSlider.h"

class TooltipOverlay : public juce::Component, private juce::Timer
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

        juce::Rectangle<float> thumbBounds = trackedSlider.getActiveThumbBoundsInComponent(*this);
        juce::String tooltipText = trackedSlider.getActiveThumbTooltipText();
        VerticalRangeSlider::ActiveThumb activeThumb = trackedSlider.getActiveThumb();

        if (thumbBounds.isEmpty() || tooltipText.isEmpty() || activeThumb == VerticalRangeSlider::NoThumb)
        {
            return;
        }

        constexpr float tooltipHeight = 24.0f;
        constexpr float tooltipHorizontalPadding = 8.0f;
        constexpr float tooltipVerticalGap = 10.0f;
        constexpr float tooltipCornerRadius = 6.0f;

        juce::Font tooltipFont(14.0f);
        graphics.setFont(tooltipFont);

        float tooltipWidth = static_cast<float>(tooltipFont.getStringWidth(tooltipText))
                             + (tooltipHorizontalPadding * 2.0f);

        float tooltipXPosition = thumbBounds.getCentreX() - (tooltipWidth * 0.5f);
        float tooltipYPosition = 0.0f;

        if (activeThumb == VerticalRangeSlider::UpperThumb)
        {
            tooltipYPosition = thumbBounds.getY() - tooltipVerticalGap - tooltipHeight;
        }
        else
        {
            tooltipYPosition = thumbBounds.getBottom() + tooltipVerticalGap;
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

        juce::Colour backgroundColour = juce::Colours::black.withAlpha(0.88f * currentAlpha);
        juce::Colour textColour = juce::Colours::white.withAlpha(currentAlpha);

        graphics.setColour(backgroundColour);
        graphics.fillRoundedRectangle(tooltipBounds, tooltipCornerRadius);

        graphics.setColour(textColour);
        graphics.drawFittedText(
            tooltipText,
            tooltipBounds.getSmallestIntegerContainer(),
            juce::Justification::centred,
            1
        );
    }

private:
    void timerCallback() override
    {
        float targetAlpha = trackedSlider.shouldShowTooltip() ? 1.0f : 0.0f;
        float fadeInSpeed = 0.07f;
        float fadeOutSpeed = 0.002f;

        if (currentAlpha < targetAlpha)

        {
            currentAlpha = juce::jmin(targetAlpha, currentAlpha + fadeInSpeed);
            repaint();
            return;
        }

        if (currentAlpha > targetAlpha)
        {
            currentAlpha = juce::jmax(targetAlpha, currentAlpha - fadeOutSpeed);
            repaint();
        }
    }

    VerticalRangeSlider& trackedSlider;
    float currentAlpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TooltipOverlay)
};