#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// FlatLabel: A filled, flat rectangle value box with centred text.
// Used as the Slider text box (created by LookAndFeel::createSliderTextBox).
class FlatLabel : public juce::Label
{
public:
    FlatLabel()
        : juce::Label()
    {
        setJustificationType(juce::Justification::centred);
        setInterceptsMouseClicks(true, true);

        // Base colours (also used when editing)
        setColour(juce::Label::backgroundColourId, AccentGray);
        setColour(juce::Label::textColourId, juce::Colours::white);

        // TextEditor colours for edit mode (when user clicks to type)
        setColour(juce::TextEditor::backgroundColourId, AccentGray);
        setColour(juce::TextEditor::textColourId, juce::Colours::white);
        setColour(juce::TextEditor::highlightColourId, ThemePink.withAlpha(0.35f));
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    }

    void paint(juce::Graphics& Graphics) override
    {
        // Flat filled rectangle
        juce::Rectangle<float> Bounds = getLocalBounds().toFloat();
        Graphics.setColour(findColour(juce::Label::backgroundColourId));
        Graphics.fillRect(Bounds);

        // Optional subtle border
        //Graphics.setColour(AccentGray.brighter(0.15f));
        //Graphics.drawRect(Bounds, 1.0f);

        // Draw text centred
        Graphics.setColour(findColour(juce::Label::textColourId));
        Graphics.setFont(getFont());
        Graphics.drawFittedText(getText(),
                                getLocalBounds(),
                                juce::Justification::centred,
                                1);

        // (Skip juce::Label::paint to avoid default look)
    }

    void editorShown(juce::TextEditor* TextEditor) override
    {
        if (TextEditor != nullptr)
        {
            // Force editor to match flat look
            TextEditor->setJustification(juce::Justification::centred);
            TextEditor->setBorder(juce::BorderSize<int>(0));
        }
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlatLabel)
};