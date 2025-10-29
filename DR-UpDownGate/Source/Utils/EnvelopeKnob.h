#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class EnvelopeKnob : public juce::Slider
{
public:
    EnvelopeKnob() : juce::Slider(juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow) {}

    juce::String getTextFromValue(double value) override
    {
        int ms = juce::roundToInt(value * 1000.0); // Map 0..1 to 0..1000 ms
        return juce::String(ms) + " ms";
    }

    double getValueFromText(const juce::String& text) override
    {
        int ms = text.getIntValue();
        return juce::jlimit(0.0, 1.0, ((double)ms / 1000.0));
    }
};