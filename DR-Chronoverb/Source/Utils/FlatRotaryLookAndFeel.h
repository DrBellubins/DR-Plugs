#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include "FlatLabel.h"

class FlatRotaryLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(
        juce::Graphics& Graphics,
        int X,
        int Y,
        int Width,
        int Height,
        float SliderPosProportional,
        float RotaryStartAngle,
        float RotaryEndAngle,
        juce::Slider& Slider) override
    {
        juce::Rectangle<float> Bounds(static_cast<float>(X),
                                      static_cast<float>(Y),
                                      static_cast<float>(Width),
                                      static_cast<float>(Height));

        float Diameter = juce::jmin(static_cast<float>(Width),
                                    static_cast<float>(Height)) - 8.0f;
        float Radius = Diameter / 2.0f;
        juce::Point<float> Center = Bounds.getCentre();

        // Background fill
        Graphics.setColour(AccentGray.brighter(0.1f));
        Graphics.fillEllipse(Center.x - Radius, Center.y - Radius, Diameter, Diameter);

        // Arc value
        float Angle = RotaryStartAngle + SliderPosProportional * (RotaryEndAngle - RotaryStartAngle);
        juce::Path ValueArc;
        ValueArc.addArc(Center.x - Radius, Center.y - Radius, Diameter, Diameter,
                        RotaryStartAngle, Angle, true);

        Graphics.setColour(ThemePink);
        Graphics.strokePath(ValueArc, juce::PathStrokeType(6.0f));
    }

    juce::Label* createSliderTextBox(juce::Slider& Slider) override
    {
        auto* ValueBox = new FlatLabel();

        // Size: tweak for your design
        int BoxWidth = 54;
        int BoxHeight = 22;

        // TextBox style already chosen by Slider (TextBoxBelow / Right)
        // We enforce size here:
        ValueBox->setSize(BoxWidth, BoxHeight);

        // Font (optional)
        juce::Font Font("Liberation Sans", 12.0f, juce::Font::bold);
        ValueBox->setFont(Font);

        // Centred text
        ValueBox->setJustificationType(juce::Justification::centred);

        // Remove any shadow/border artifacts
        ValueBox->setBorderSize(juce::BorderSize<int>(0));

        // Colours already set in FlatLabel constructor; ensure consistency:
        ValueBox->setColour(juce::Label::backgroundColourId, AccentGray);
        ValueBox->setColour(juce::Label::textColourId, juce::Colours::white);

        // Ensure slider's colour lookups do not override
        Slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

        return ValueBox;
    }

    void drawTextEditorOutline(juce::Graphics& Graphics,
                               int Width,
                               int Height,
                               juce::TextEditor& TextEditor) override
    {
        juce::ignoreUnused(TextEditor);

        // Flat editor outline (if we ever allow focus border)
        Graphics.setColour(AccentGray.brighter(0.1));
        Graphics.drawRect(0, 0, Width, Height, 2);
    }
};