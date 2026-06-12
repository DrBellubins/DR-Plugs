#include "DistortionRackLayout.h"

void DistortionRackLayout::CreateDistortionRackLayout(
    juce::Component& parent,
    AudioPluginAudioProcessor& processorRef,
    const RackTheme& theme,
    int x, int y, int width, int height)
{
    rack.CreateRackLayout(parent, processorRef, theme, x, y, width, height);

    const DistortionModuleParameterIDs module1IDs
    {
        "distortionMod1Enabled",
        "distortionMod1Type",
        "distortionMod1Drive",
        "distortionMod1Mix",
        "distortionMod1Target",
        "distortionMod1PrePost"
    };

    const DistortionModuleParameterIDs module2IDs
    {
        "distortionMod2Enabled",
        "distortionMod2Type",
        "distortionMod2Drive",
        "distortionMod2Mix",
        "distortionMod2Target",
        "distortionMod2PrePost"
    };

    const DistortionModuleParameterIDs module3IDs
    {
        "distortionMod3Enabled",
        "distortionMod3Type",
        "distortionMod3Drive",
        "distortionMod3Mix",
        "distortionMod3Target",
        "distortionMod3PrePost"
    };

    auto& apvts = processorRef.parameters;

    distortionModule1.CreateLayout(theme, apvts, module1IDs);
    distortionModule2.CreateLayout(theme, apvts, module2IDs);
    distortionModule3.CreateLayout(theme, apvts, module3IDs);

    rack.RegisterModule(distortionModule1);
    rack.RegisterModule(distortionModule2);
    rack.RegisterModule(distortionModule3);
}