#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../RackTheme.h"
#include "ModuleThemeProvider.h"

using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

class Module : public juce::Component,
               public ModuleThemeProvider
{
public:
    Module();
    ~Module() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

    void SetThemeColour(juce::Colour newThemeColour);
    juce::Colour GetThemeColour() const override;

    void SetModuleEnabled(bool shouldBeEnabled);
    bool IsModuleEnabled() const;

    juce::Component& GetOscilloscopePlaceholder();
    juce::ToggleButton& GetEnableButton();

    void AttachEnableButton(juce::AudioProcessorValueTreeState& apvts,
                            const juce::String& parameterID);

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
    std::unique_ptr<ButtonAttachment> enableAttachment;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Module)
};