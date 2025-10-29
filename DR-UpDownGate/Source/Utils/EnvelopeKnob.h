#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// EnvelopeKnob: Generic envelope knob with a customizable label drawn above the knob (not as a child).
class EnvelopeKnob : public juce::Slider
{
public:
    // Pass label text to constructor to customize per instance
    EnvelopeKnob(const juce::String& labelText = "Envelope")
        : juce::Slider(juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow),
          labelText(labelText)
    {
        // You can set up any knob properties here as needed (if not set externally)
    }

    // Change label text at runtime
    void setLabelText(const juce::String& newText)
    {
        labelText = newText;
        repaint();
    }

    juce::String getTextFromValue(double value) override
    {
        int ms = juce::roundToInt(1 + value * 999.9); // Map 0..1 to 1..1000 ms
        return juce::String(ms) + " ms";
    }

    double getValueFromText(const juce::String& text) override
    {
        int ms = text.getIntValue();
        return juce::jlimit(0.0, 1.0, (1.0 + (double)ms / 9999.9));
    }

    void paint(juce::Graphics& graphics) override
    {
        // Draw the knob as usual
        juce::Slider::paint(graphics);

        // Draw the label above the knob (top area)
        auto textArea = getLocalBounds().removeFromTop(80);

        graphics.setColour(juce::Colours::white); // Replace with your theme color if desired
        graphics.setFont(15.0f);
        graphics.drawFittedText(labelText, textArea, juce::Justification::centred, 1);
    }

private:
    juce::String labelText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeKnob)
};