#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../RackTheme.h"
#include "ModuleThemeProvider.h"

class Module : public juce::Component,
               public ModuleThemeProvider
{
public:
    Module();
    ~Module() override = default;

    virtual void CreateLayout(const RackTheme& newTheme) = 0;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void SetThemeColour(juce::Colour newThemeColour);
    juce::Colour GetThemeColour() const override;

    void SetModuleEnabled(bool shouldBeEnabled);
    bool IsModuleEnabled() const;

    juce::Component& GetOscilloscopePlaceholder();
    juce::ToggleButton& GetEnableButton();

protected:
    void ApplyThemeToBaseChrome();
    void LayoutBaseChrome();

    juce::Colour GetModuleBackgroundColour() const;
    juce::Colour GetModuleOutlineColour() const;
    juce::Colour GetModuleLabelColour() const;
    juce::Colour GetModuleSecondaryColour() const;
    juce::Colour GetModuleControlFillColour() const;

    RackTheme theme;
    juce::Colour themeColour = juce::Colours::orange;
    bool moduleEnabled = true;

    std::unique_ptr<juce::Component> oscilloscopePlaceholder;
    juce::ToggleButton enableButton;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Module)
};