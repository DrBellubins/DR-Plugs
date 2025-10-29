#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "FlatTextBox.h"

class FlatRotaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (
        juce::Graphics& graphics, int x, int y, int width, int height,
        float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
        juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>(x, y, width, height);
        float diameter = juce::jmin(width, height) - 8.0f;
        auto radius = diameter / 2.0f;
        auto center = bounds.getCentre();

        // Background
        graphics.setColour(AccentGray);
        graphics.fillEllipse(center.x - radius, center.y - radius, diameter, diameter);

        // Value arc
        float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        juce::Path valueArc;
        valueArc.addArc(center.x - radius, center.y - radius, diameter, diameter,
                        rotaryStartAngle, angle, true);

        graphics.setColour(ThemePink);
        graphics.strokePath(valueArc, juce::PathStrokeType(6.0f));
    }

    juce::Label* createSliderTextBox(juce::Slider&) override
    {
        auto* box = new FlatTextBox();
        return box;
    }

    void drawTextEditorOutline(juce::Graphics& graphics, int width, int height, juce::TextEditor& textEditor) override
    {
        const int thickness = (textEditor.hasKeyboardFocus(false) ? 2 : 1);

        graphics.setColour(FocusedGray);
        graphics.drawRect(0, 0, width, height, thickness);
    }
};