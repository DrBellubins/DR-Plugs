#pragma once

#include "PluginProcessor.h"
#include "Utils/ThemedCheckbox.h"
#include "Utils/ThemedKnob.h"

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    std::unique_ptr<ThemedKnob> arpRateKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> arpRateAttachment;

    std::unique_ptr<ThemedCheckbox> freeModeCheckbox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freeModeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
