#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// EnvelopeKnob: Generic envelope knob with a customizable label above the knob.
class EnvelopeKnob : public juce::Slider
{
public:
    // Pass label text to constructor to customize per instance
    EnvelopeKnob(const juce::String& labelText = "Envelope")
        : juce::Slider(juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow),
          label({}, labelText)
    {
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::white); // Replace with theme if desired
        addAndMakeVisible(label);
    }

    // Change label text at runtime
    void setLabelText(const juce::String& newText)
    {
        label.setText(newText, juce::dontSendNotification);
    }

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

    void resized() override
    {
        auto bounds = getLocalBounds();
        label.setBounds(bounds.removeFromTop(20)); // 20 px tall label at top
        // The rest is handled by juce::Slider
    }

private:
    juce::Label label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeKnob)
};