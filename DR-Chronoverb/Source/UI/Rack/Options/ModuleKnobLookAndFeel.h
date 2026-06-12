#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ModuleKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& graphics,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(width),
            static_cast<float>(height));

        const float diameter =
            juce::jmin(static_cast<float>(width), static_cast<float>(height)) - 8.0f;

        const float radius = diameter * 0.5f;
        const auto centre = bounds.getCentre();

        const auto fillColour = juce::Colours::black;

        const auto arcColour =
            slider.findColour(juce::Slider::rotarySliderFillColourId);

        graphics.setColour(fillColour.brighter(0.08f));
        graphics.fillEllipse(centre.x - radius, centre.y - radius, diameter, diameter);

        const float angle =
            rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        juce::Path valueArc;
        valueArc.addArc(centre.x - radius,
                        centre.y - radius,
                        diameter,
                        diameter,
                        rotaryStartAngle,
                        angle,
                        true);

        graphics.setColour(arcColour);
        graphics.strokePath(valueArc,
                            juce::PathStrokeType(4.0f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::butt));
    }
};