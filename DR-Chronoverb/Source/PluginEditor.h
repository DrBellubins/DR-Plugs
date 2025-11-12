#pragma once

#include "PluginProcessor.h"
#include "Utils/FlatLabel.h"
#include "Utils/ThemedKnob.h"

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void createLabel(std::unique_ptr<juce::Label>& label, juce::String Text);

    int getLabelWidth(std::unique_ptr<juce::Label>& label);

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    // Knobs
    std::unique_ptr<ThemedKnob> delayTimeKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayTimeAttachment;

    std::unique_ptr<ThemedKnob> feedbackTimeKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackTimeAttachment;

    std::unique_ptr<ThemedKnob> diffusionAmountKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionAmountAttachment;

    std::unique_ptr<ThemedKnob> diffusionSizeKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionSizeAttachment;

    std::unique_ptr<ThemedKnob> diffusionQualityKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionQualityAttachment;

    std::unique_ptr<ThemedKnob> dryWetMixKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetMixAttachment;

    // Labels
    std::unique_ptr<juce::Label> delayTimeLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
