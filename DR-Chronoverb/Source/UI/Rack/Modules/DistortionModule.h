#pragma once

#include "../ModuleSystem/Module.h"
#include "../Options/ModuleDropdown.h"
#include "../Options/ModuleKnob.h"
#include "../Options/ModuleSegmentedButton.h"

struct DistortionModuleParameterIDs
{
    juce::String enabled;
    juce::String type;
    juce::String drive;
    juce::String mix;
    juce::String target;
    juce::String prePost;
};

class DistortionModule : public Module
{
public:
    DistortionModule();
    ~DistortionModule() override = default;

    void CreateLayout(const RackTheme& newTheme,
                      juce::AudioProcessorValueTreeState& apvts,
                      const DistortionModuleParameterIDs& parameterIDs);

    void resized() override;

    ModuleDropdown& GetTypeDropdown();
    ModuleKnob& GetDriveKnob();
    ModuleKnob& GetMixKnob();
    ModuleSegmentedButton& GetTargetSegmented();

    juce::Label& GetTitleLabel();

    ModuleDropdown typeDropdown;
    ModuleKnob driveKnob;
    ModuleKnob mixKnob;
    ModuleSegmentedButton targetSegmented;

private:
    void ApplyThemeToControls();
    void LayoutControls();

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionModule)
};