#pragma once

#include "PluginProcessor.h"
#include "Utils/FlatLabel.h"
#include "Utils/ThemedKnob.h"
#include "BinaryData.h"

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void createKnob(std::unique_ptr<ThemedKnob>& knob, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment, juce::String paramID,
        juce::String suffix, int widthHeight, int offsetFromCenterX, int offsetFromCenterY);

    void createKnobLabel(std::unique_ptr<juce::Label>& label, ThemedKnob& knob,
        juce::String text, float fontSize, int offsetY);

    int getLabelWidth(std::unique_ptr<juce::Label>& label);

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    juce::Image background;
    juce::Image logo;

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
    std::unique_ptr<juce::Label> feedbackLabel;
    std::unique_ptr<juce::Label> diffusionAmountLabel;
    std::unique_ptr<juce::Label> diffusionSizeLabel;
    std::unique_ptr<juce::Label> diffusionQualityLabel;
    std::unique_ptr<juce::Label> dryWetMixLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
