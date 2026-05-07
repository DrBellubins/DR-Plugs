#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "FlatTextBox.h"

class FlatRotaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(
        juce::Graphics& Graphics,
        int X,
        int Y,
        int Width,
        int Height,
        float SliderPositionProportional,
        float RotaryStartAngle,
        float RotaryEndAngle,
        juce::Slider& Slider) override
    {
        juce::ignoreUnused(Slider);

        juce::Rectangle<int> Bounds(X, Y, Width, Height);

        float Diameter = juce::jmin(static_cast<float>(Width), static_cast<float>(Height)) - 8.0f;
        float Radius = Diameter * 0.5f;
        juce::Point<float> Centre = Bounds.toFloat().getCentre();

        juce::Rectangle<float> KnobBounds(
            Centre.x - Radius,
            Centre.y - Radius,
            Diameter,
            Diameter
        );

        juce::Path KnobPath;
        KnobPath.addEllipse(KnobBounds);

        juce::DropShadow KnobShadow(
            juce::Colours::black.withAlpha(0.25f),
            10,
            juce::Point<int>(0, 10)
        );

        KnobShadow.drawForPath(Graphics, KnobPath);

        Graphics.setColour(AccentGray.brighter(0.05f));
        Graphics.fillEllipse(KnobBounds);

        float Angle = RotaryStartAngle
                      + SliderPositionProportional * (RotaryEndAngle - RotaryStartAngle);

        juce::Path ValueArc;
        ValueArc.addArc(
            KnobBounds.getX(),
            KnobBounds.getY(),
            KnobBounds.getWidth(),
            KnobBounds.getHeight(),
            RotaryStartAngle,
            Angle,
            true
        );

        Graphics.setColour(ThemePink);
        Graphics.strokePath(ValueArc, juce::PathStrokeType(6.0f));
    }

    juce::Label* createSliderTextBox(juce::Slider& Slider) override
    {
        juce::ignoreUnused(Slider);

        return new FlatTextBox();
    }

    void drawTextEditorOutline(
        juce::Graphics& Graphics,
        int Width,
        int Height,
        juce::TextEditor& TextEditor) override
    {
        int Thickness = TextEditor.hasKeyboardFocus(false) ? 2 : 1;

        Graphics.setColour(FocusedGray);
        Graphics.drawRect(0, 0, Width, Height, Thickness);
    }
};