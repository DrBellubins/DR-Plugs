#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemedKnob.h"

// ThemedKnobWithLabel: Combines a ThemedKnob and a label above it, with adjustable label height.
class ThemedKnobWithLabel : public juce::Component
{
public:
    explicit ThemedKnobWithLabel(
        const juce::String& LabelText,
        ThemedKnob::ValueToTextFunction ValueToTextFunction = nullptr,
        ThemedKnob::TextToValueFunction TextToValueFunction = nullptr,
        const juce::String& Suffix = "",
        juce::Slider::TextEntryBoxPosition TextBoxPosition = juce::Slider::TextBoxBelow)
        : labelHeight(20)
    {
        label.setText(LabelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::white); // Style as desired
        addAndMakeVisible(label);

        addAndMakeVisible(knob);
        knob.setValueToTextFunction(ValueToTextFunction);
        knob.setTextToValueFunction(TextToValueFunction);
        knob.setValueSuffix(Suffix);
        knob.setTextBoxStyle(TextBoxPosition, false, 48, 18); // Adjust as preferred
    }

    void setLabelHeight(int NewLabelHeight)
    {
        if (labelHeight != NewLabelHeight)
        {
            labelHeight = NewLabelHeight;
            resized();
            repaint();
        }
    }

    int getLabelHeight() const
    {
        return labelHeight;
    }

    ThemedKnob& getKnob()
    {
        return knob;
    }

    juce::Label& getLabel()
    {
        return label;
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds(area.removeFromTop(labelHeight));
        knob.setBounds(area);
    }

private:
    juce::Label label;
    ThemedKnob knob;
    int labelHeight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedKnobWithLabel)
};