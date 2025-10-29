#pragma once

#include "PluginProcessor.h"
#include "Utils/VerticalRangeSlider.h"

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    const juce::Colour BGGray = juce::Colour(50, 50, 50);
    const juce::Colour AccentGray = juce::Colour(40, 40, 40);
    const juce::Colour ThemePink = juce::Colour(255, 140, 230);

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    std::unique_ptr<VerticalRangeSlider> gateRangeSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
