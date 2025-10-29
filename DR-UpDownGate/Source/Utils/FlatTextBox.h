#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// Subclass juce::Label, not juce::TextEditor!
class FlatTextBox : public juce::Label
{
public:
    FlatTextBox() : juce::Label() {}

    void paint(juce::Graphics& g) override
    {
        juce::Label::paint(g);

        // Draw a custom outline (always, focused or not)
        auto rect = getLocalBounds().toFloat();
        g.setColour(AccentGray);
        g.drawRect(rect, 2.0f); // 2px thickness
    }
};