#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemedKnob.h"

// Arranges a label above a knob, allowing label width to exceed knob width.
class ThemedKnobWithLabel : public juce::Component
{
public:
    ThemedKnobWithLabel(const juce::String& LabelText,
                   ThemedKnob::ValueToTextFunction ValueToTextFunction = nullptr,
                   ThemedKnob::TextToValueFunction TextToValueFunction = nullptr,
                   const juce::String& Suffix = "",
                   juce::Slider::TextEntryBoxPosition TextBoxPosition = juce::Slider::TextBoxBelow)
    {
        addAndMakeVisible(label);
        label.setText(LabelText, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::white);

        addAndMakeVisible(knob);
        knob.setValueToTextFunction(ValueToTextFunction);
        knob.setTextToValueFunction(TextToValueFunction);
        knob.setValueSuffix(Suffix);
        knob.setTextBoxStyle(TextBoxPosition, false, 48, 18);
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
        // Label can be full width, knob is centered and narrower.
        auto area = getLocalBounds();
        const int labelHeight = 22; // or make adjustable
        label.setBounds(area.removeFromTop(labelHeight));

        int knobSize = juce::jmin(area.getWidth(), area.getHeight());
        juce::Rectangle<int> knobBounds = area.withSizeKeepingCentre(knobSize, knobSize);
        knob.setBounds(knobBounds);
    }

private:
    juce::Label label;
    ThemedKnob knob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedKnobWithLabel)
};