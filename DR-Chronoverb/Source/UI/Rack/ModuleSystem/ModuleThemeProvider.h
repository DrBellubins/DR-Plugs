#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ModuleThemeProvider
{
public:
    virtual ~ModuleThemeProvider() = default;

    virtual juce::Colour GetThemeColour() const = 0;
};