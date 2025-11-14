#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "juce_core/unit_tests/juce_UnitTestCategories.h"

// ThemedKnob: Customizable rotary knob with a label and flexible value/text handling.
class ThemedKnob : public juce::Slider
{
public:
    // Format function: (double value) -> juce::String for knob text
    using ValueToTextFunction = std::function<juce::String(double Value)>;

    // Parse function: (juce::String text) -> double value
    using TextToValueFunction = std::function<double(const juce::String& Text)>;

    // Constructor: pass label text and, optionally, text/value conversion functions, suffix, and textbox position
    ThemedKnob(const juce::String& LabelText = "Knob",
               ValueToTextFunction ToTextFunction = nullptr,
               TextToValueFunction ToValueFunction = nullptr,
               const juce::String& Suffix = "",
               juce::Slider::TextEntryBoxPosition TextBoxPosition = juce::Slider::TextBoxBelow)
        : juce::Slider(juce::Slider::RotaryVerticalDrag, TextBoxPosition),
          labelText(LabelText),
          valueToTextFunction(ToTextFunction),
          textToValueFunction(ToValueFunction),
          valueSuffix(Suffix)
    {
        setTextValueSuffix(valueSuffix);
    }

    // Set label text at runtime
    void setLabelText(const juce::String& NewText)
    {
        labelText = NewText;
        repaint();
    }

    juce::String getTextFromValue(double Value) override
    {
        if (valueToTextFunction)
        {
            return valueToTextFunction(Value);
        }

        // Default: show value with suffix
        if (!valueSuffix.isEmpty())
        {
            return juce::String(Value, 2) + " " + valueSuffix;
        }
        else
        {
            return juce::String(Value, 2);
        }
    }

    double getValueFromText(const juce::String& Text) override
    {
        if (textToValueFunction)
        {
            return textToValueFunction(Text);
        }

        // Default: parse as double
        juce::String textNoSuffix = Text;

        if (!valueSuffix.isEmpty() && textNoSuffix.endsWith(valueSuffix))
        {
            textNoSuffix = textNoSuffix.dropLastCharacters(valueSuffix.length()).trim();
        }

        return textNoSuffix.getDoubleValue();
    }

    void setValueToTextFunction(ValueToTextFunction Function)
    {
        valueToTextFunction = Function;
        repaint();
    }

    void setTextToValueFunction(TextToValueFunction Function)
    {
        textToValueFunction = Function;
    }

    void setValueSuffix(const juce::String& Suffix)
    {
        valueSuffix = Suffix;
        setTextValueSuffix(valueSuffix);
        repaint();
    }

    void paint(juce::Graphics& Graphics) override
    {
        juce::Slider::paint(Graphics);

        // replicate arc/ellipse calculation from FlatRotaryLookAndFeel
        juce::Rectangle<int> KnobArea = getLocalBounds();

        if (getTextBoxPosition() == juce::Slider::TextBoxBelow && getTextBoxHeight() > 0)
            KnobArea.setHeight(KnobArea.getHeight() - getTextBoxHeight());

        int width = KnobArea.getWidth();
        int height = KnobArea.getHeight();
        int size = juce::jmin(width, height);

        float diameter = static_cast<float>(size) - 20.0f;
        float radius = diameter / 2.0f;
        juce::Point<float> center = KnobArea.toFloat().getCentre();

        // Now draw the inner circle using the same base as the arc
        Graphics.setColour(AccentGray);
        Graphics.fillEllipse(center.x - radius, center.y - radius, diameter, diameter);
    }

private:
    juce::String labelText;
    ValueToTextFunction valueToTextFunction;
    TextToValueFunction textToValueFunction;
    juce::String valueSuffix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedKnob)
};