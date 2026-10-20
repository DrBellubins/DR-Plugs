#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// Subclass juce::Label, not juce::TextEditor!
class FlatLabel : public juce::Label
{
public:
    FlatLabel() : juce::Label() {}

    void paint(juce::Graphics& graphics) override
    {
        juce::Label::paint(graphics);

        // Draw a custom outline (always, focused or not)
        auto rect = getLocalBounds().toFloat();
        graphics.setColour(AccentGray);
        graphics.drawRect(rect, 2.0f); // 2px thickness
    }
};