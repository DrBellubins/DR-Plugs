#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VerticalRangeSlider.h"

class SliderTooltipOverlay : public juce::Component
{
public:
    explicit SliderTooltipOverlay(VerticalRangeSlider& sliderToTrack)
        : trackedSlider(sliderToTrack)
    {
        setInterceptsMouseClicks(false, false);
        setAlwaysOnTop(true);
    }

    void paint(juce::Graphics& graphics) override
    {
        if (!trackedSlider.shouldShowTooltip())
        {
            return;
        }

        juce::Rectangle<float> thumbBoundsInParent = trackedSlider.getActiveThumbBoundsInParent();
        juce::String tooltipText = trackedSlider.getActiveThumbTooltipText();

        if (thumbBoundsInParent.isEmpty() || tooltipText.isEmpty())
        {
            return;
        }

        constexpr float tooltipPaddingX = 8.0f;
        constexpr float tooltipHeight = 24.0f;
        constexpr float tooltipGap = 10.0f;
        constexpr float tooltipCornerRadius = 6.0f;

        juce::Font tooltipFont(14.0f);
        graphics.setFont(tooltipFont);

        float tooltipWidth = static_cast<float>(tooltipFont.getStringWidth(tooltipText)) + (tooltipPaddingX * 2.0f);

        float tooltipXPosition = thumbBoundsInParent.getRight() + tooltipGap;
        float tooltipYPosition = thumbBoundsInParent.getCentreY() - (tooltipHeight * 0.5f);

        juce::Rectangle<float> tooltipBounds(
            tooltipXPosition,
            tooltipYPosition,
            tooltipWidth,
            tooltipHeight
        );

        juce::Rectangle<float> overlayBounds = getLocalBounds().toFloat();

        if (tooltipBounds.getRight() > overlayBounds.getRight())
        {
            tooltipBounds.setX(thumbBoundsInParent.getX() - tooltipGap - tooltipWidth);
        }

        if (tooltipBounds.getY() < overlayBounds.getY())
        {
            tooltipBounds.setY(overlayBounds.getY());
        }

        if (tooltipBounds.getBottom() > overlayBounds.getBottom())
        {
            tooltipBounds.setY(overlayBounds.getBottom() - tooltipHeight);
        }

        graphics.setColour(juce::Colours::black.withAlpha(0.88f));
        graphics.fillRoundedRectangle(tooltipBounds, tooltipCornerRadius);

        graphics.setColour(juce::Colours::white);
        graphics.drawFittedText(
            tooltipText,
            tooltipBounds.getSmallestIntegerContainer(),
            juce::Justification::centred,
            1
        );
    }

private:
    VerticalRangeSlider& trackedSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderTooltipOverlay)
};