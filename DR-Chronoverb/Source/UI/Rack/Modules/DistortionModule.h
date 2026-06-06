#pragma once

#include "../ModuleSystem/Module.h"

class DistortionModule : public Module
{
public:
    DistortionModule();
    ~DistortionModule() override = default;

    void CreateLayout(const RackTheme& newTheme) override;
    void resized() override;

    juce::ComboBox& GetTypeDropdown();
    juce::Slider& GetDriveKnob();
    juce::Slider& GetMixKnob();

    juce::Label& GetTitleLabel();
    juce::Label& GetTypeLabel();
    juce::Label& GetDriveLabel();
    juce::Label& GetMixLabel();

private:
    void ApplyThemeToControls();
    void LayoutControls();

    juce::Label titleLabel;
    juce::ComboBox typeDropdown;
    juce::Label typeLabel;

    juce::Slider driveKnob;
    juce::Label driveLabel;

    juce::Slider mixKnob;
    juce::Label mixLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionModule)
};
