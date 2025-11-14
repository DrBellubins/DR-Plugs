#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Theme.h"

// ThemedSlider: Flat horizontal slider matching ThemedKnob, full rect, no thumb, no shadow.
class ThemedSlider : public juce::Slider
{
public:
    // Function type to convert slider value to text.
    using ValueToTextFunction = std::function<juce::String(double Value)>;

    // Function type to convert text to slider value.
    using TextToValueFunction = std::function<double(const juce::String& Text)>;

    // Constructor
    ThemedSlider(const juce::String& LabelText = "Slider",
                 ValueToTextFunction ToTextFunction = nullptr,
                 TextToValueFunction ToValueFunction = nullptr,
                 const juce::String& Suffix = "",
                 juce::Slider::TextEntryBoxPosition TextBoxPosition = juce::Slider::TextBoxRight)
        : juce::Slider(juce::Slider::LinearHorizontal, TextBoxPosition),
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

        juce::Rectangle<int> SliderArea = getLocalBounds();

        // Background track (full bounds, flat, no shadow)
        Graphics.setColour(AccentGray);
        Graphics.fillRect(SliderArea);

        // Value fill (overlay, full height)
        double Proportion = (getValue() - getMinimum()) / (getMaximum() - getMinimum());

        int FillWidth = static_cast<int>(SliderArea.getWidth() * Proportion);
        juce::Rectangle<int> ValueRect = SliderArea.withWidth(FillWidth);

        Graphics.setColour(ThemePink);
        Graphics.fillRect(ValueRect);
    }

private:
    juce::String labelText;
    ValueToTextFunction valueToTextFunction;
    TextToValueFunction textToValueFunction;
    juce::String valueSuffix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedSlider)
};