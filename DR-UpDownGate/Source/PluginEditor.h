#pragma once

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Utils/Theme.h"
#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/EnvelopeKnob.h"
#include "PluginProcessor.h"
#include "Utils/VerticalRangeSlider.h"
#include "Utils/VerticalRangeSliderAttachment.h"

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    float getRangeLow() const { return rangeSlider ? rangeSlider->getLowerValue() : 0.0f; }
    float getRangeHigh() const { return rangeSlider ? rangeSlider->getUpperValue() : 1.0f; }
    float getAttack() const { return attackKnob ? attackKnob->getValue() : 0.0f; }
    float getRelease() const { return releaseKnob ? releaseKnob->getValue() : 0.0f; }

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    std::unique_ptr<VerticalRangeSlider> rangeSlider;
    std::unique_ptr<VerticalRangeSliderAttachment> rangeSliderAttachment;

    std::unique_ptr<juce::Slider> attackKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackKnobAttacthment;

    std::unique_ptr<juce::Slider> releaseKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseKnobAttacthment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
