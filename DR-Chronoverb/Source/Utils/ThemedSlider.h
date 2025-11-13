#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Theme.h"

// ThemedSlider: Customizable horizontal slider with themed appearance, matching ThemedKnob.
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

        // Draw track background
        juce::Rectangle<int> trackArea = getLocalBounds().reduced(4, getHeight() / 3);
        Graphics.setColour(AccentGray);
        Graphics.fillRect(trackArea.withHeight(6));

        // Draw value fill
        double proportion = (getValue() - getMinimum()) / (getMaximum() - getMinimum());
        int fillWidth = static_cast<int>(trackArea.getWidth() * proportion);
        juce::Rectangle<int> valueRect = trackArea.withWidth(fillWidth);

        Graphics.setColour(ThemePink);
        Graphics.fillRect(valueRect.withHeight(6));

        // Draw thumb
        int thumbX = trackArea.getX() + fillWidth;
        int thumbY = trackArea.getCentreY();
        int thumbRadius = 8;
        Graphics.setColour(juce::Colours::white);
        Graphics.fillEllipse(static_cast<float>(thumbX - thumbRadius), 
                            static_cast<float>(thumbY - thumbRadius),
                            static_cast<float>(2 * thumbRadius),
                            static_cast<float>(2 * thumbRadius));
        Graphics.setColour(FocusedGray);
        Graphics.drawEllipse(static_cast<float>(thumbX - thumbRadius),
                             static_cast<float>(thumbY - thumbRadius),
                             static_cast<float>(2 * thumbRadius),
                             static_cast<float>(2 * thumbRadius),
                             2.0f);
    }

private:
    juce::String labelText;
    ValueToTextFunction valueToTextFunction;
    TextToValueFunction textToValueFunction;
    juce::String valueSuffix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedSlider)
};