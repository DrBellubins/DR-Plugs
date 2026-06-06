#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../RackTheme.h"
#include "../ModuleSystem/ModuleThemeProvider.h"

class ModuleOption : public juce::Component
{
public:
    ModuleOption();
    ~ModuleOption() override = default;

    void SetLabelText(const juce::String& newText);
    const juce::String GetLabelText() const;

    void SetLabelVisible(bool shouldBeVisible);
    bool IsLabelVisible() const;

    juce::Label& GetLabel();

    virtual void ApplyTheme(const RackTheme& rackTheme);
    virtual void ApplyEnabledState(bool shouldBeEnabled, float disabledAlpha);

protected:
    juce::Colour FindThemeColour() const;

    juce::Colour GetOptionLabelColour(const RackTheme& rackTheme) const;
    juce::Colour GetOptionOutlineColour(const RackTheme& rackTheme) const;
    juce::Colour GetOptionFillColour(const RackTheme& rackTheme) const;
    juce::Colour GetOptionAccentColour() const;

    juce::Rectangle<int> GetLabelBoundsBelow(const juce::Rectangle<int>& controlBounds,
                                             int labelHeight,
                                             int offset) const;

    RackTheme currentTheme;
    juce::Label label;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleOption)
};