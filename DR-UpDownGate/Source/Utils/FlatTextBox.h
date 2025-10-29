// Source/Utils/FlatTextBox.h
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

class FlatTextBox : public juce::TextEditor
{
public:
    FlatTextBox()
    {
        setColour(outlineColourId, juce::Colours::transparentBlack); // Prevent JUCE drawing
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        auto rect = getLocalBounds().toFloat();
        int thickness = hasKeyboardFocus(false) ? 2 : 2; // always thick, tweak as needed

        g.setColour(SecondaryAccentGray);
        g.drawRect(rect, (float)thickness);
    }
};