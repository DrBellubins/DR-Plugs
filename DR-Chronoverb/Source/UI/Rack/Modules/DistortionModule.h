#pragma once

#include "../ModuleSystem/Module.h"
#include "../Options/ModuleDropdown.h"
#include "../Options/ModuleKnob.h"

struct DistortionModuleParameterIDs
{
    juce::String enabled;
    juce::String type;
    juce::String drive;
    juce::String mix;
};

class DistortionModule : public Module
{
public:
    DistortionModule();
    ~DistortionModule() override = default;

    void CreateLayout(const RackTheme& newTheme,
                  juce::AudioProcessorValueTreeState& apvts,
                  const DistortionModuleParameterIDs& parameterIDs) override;
    
    void resized() override;

    ModuleDropdown& GetTypeDropdown();
    ModuleKnob& GetDriveKnob();
    ModuleKnob& GetMixKnob();

    juce::Label& GetTitleLabel();

    ModuleDropdown typeDropdown;
    ModuleKnob driveKnob;
    ModuleKnob mixKnob;

private:
    void ApplyThemeToControls();
    void LayoutControls();

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionModule)
};