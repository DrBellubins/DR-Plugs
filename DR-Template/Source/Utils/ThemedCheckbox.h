#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// ThemedCheckbox: Custom checkbox using ThemePink and AccentGray colors, with a rounded square indicator.
class ThemedCheckbox : public juce::ToggleButton
{
public:
    ThemedCheckbox(const juce::String& CheckboxText = juce::String())
        : juce::ToggleButton(CheckboxText)
    {
    }

    void paintButton(
        juce::Graphics& Graphics,
        bool ShouldDrawButtonAsHighlighted,
        bool ShouldDrawButtonAsDown
    ) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Calculate checkbox size and positions
        float outerSize = juce::jmin(bounds.getHeight(), 28.0f);
        float innerSize = outerSize * 0.6f;
        float outerRadius = outerSize / 4.0f;
        float innerRadius = innerSize / 4.0f;

        float boxX = bounds.getX();
        float boxY = bounds.getCentreY() - outerSize / 2.0f;

        // Outer rounded square (AccentGray)
        juce::Rectangle<float> outerRect(boxX, boxY, outerSize, outerSize);
        Graphics.setColour(AccentGray);
        Graphics.fillRoundedRectangle(outerRect, outerRadius);

        // If checked, draw the inner rounded square (ThemePink)
        if (getToggleState())
        {
            float innerX = boxX + (outerSize - innerSize) / 2.0f;
            float innerY = boxY + (outerSize - innerSize) / 2.0f;
            juce::Rectangle<float> innerRect(innerX, innerY, innerSize, innerSize);
            Graphics.setColour(ThemePink);
            Graphics.fillRoundedRectangle(innerRect, innerRadius);
        }

        // Optional: Draw focus outline if highlighted or down
        if (ShouldDrawButtonAsHighlighted || ShouldDrawButtonAsDown)
        {
            Graphics.setColour(FocusedGray);
            Graphics.drawRoundedRectangle(outerRect, outerRadius, 2.0f);
        }

        // Draw label text
        Graphics.setColour(juce::Colours::white);
        Graphics.setFont(15.0f);

        float textX = boxX + outerSize + 8.0f;
        float textY = bounds.getY();
        float textWidth = bounds.getWidth() - textX;
        float textHeight = bounds.getHeight();

        juce::Rectangle<float> textRect(textX, textY, textWidth, textHeight);
        Graphics.drawFittedText(getButtonText(), textRect.toNearestInt(), juce::Justification::centredLeft, 1);
    }
};