#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../../PluginProcessor.h"
#include "Rack.h"
#include "Modules/DistortionModule.h"

class DistortionRackLayout
{
public:
    void CreateDistortionRackLayout(juce::Component& parent,
                                    AudioPluginAudioProcessor& processorRef,
                                    const RackTheme& theme,
                                    int x, int y, int width, int height);

    Rack rack;

    DistortionModule distortionModule1;
    DistortionModule distortionModule2;
    DistortionModule distortionModule3;
};