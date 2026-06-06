#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../PluginProcessor.h"
#include "RackTheme.h"
#include "Modules/DistortionModule.h"

class Rack : public juce::Component
{
public:
    Rack() = default;
    ~Rack() override = default;

    void CreateRackLayout(juce::Component& parent,
                          AudioPluginAudioProcessor& processorRef,
                          const RackTheme& newTheme,
                          int x, int y, int width, int height);

    void paint(juce::Graphics& g) override;
    void resized() override;

    DistortionModule distortionModule;

private:
    void LayoutModules();

    AudioPluginAudioProcessor* processor = nullptr;
    RackTheme theme;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Rack)
};