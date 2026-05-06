#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "VerticalRangeSlider.h"

class GateLevelDisplay : public juce::Component,
                         private juce::Timer
{
public:
    GateLevelDisplay(AudioPluginAudioProcessor& audioProcessor,
                     VerticalRangeSlider& thresholdSlider);

    ~GateLevelDisplay() override = default;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    float decibelsToY(float decibelValue) const;

    AudioPluginAudioProcessor& processorRef;
    VerticalRangeSlider& rangeSliderRef;

    float currentLevelDecibels = -60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GateLevelDisplay)
};