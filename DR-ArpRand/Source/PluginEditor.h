#pragma once

#include "PluginProcessor.h"
#include "Utils/HorizontalRangeSlider.h"
#include "Utils/HorizontalRangeSliderAttachment.h"
#include "Utils/SteppedHorizontalRangeSlider.h"
#include "Utils/ThemedCheckbox.h"
#include "Utils/ThemedKnob.h"

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer,
	private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

	void parameterChanged(const juce::String& parameterID, float newValue) override
	{
		if (parameterID == "isOctaves" && octaveRangeSlider)
		{
			juce::MessageManager::callAsync([this, state = (newValue >= 0.5f)]()
			{
				if (state)
					octaveRangeSlider->Enable();
				else
					octaveRangeSlider->Disable();
			});
		}
	}

private:
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    std::unique_ptr<ThemedKnob> arpRateKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> arpRateAttachment;

	std::unique_ptr<SteppedHorizontalRangeSlider> octaveRangeSlider;
	std::unique_ptr<HorizontalRangeSliderAttachment> octaveRangeSliderAttachment;

	std::unique_ptr<juce::Label> octaveRangeLowLabel;
	std::unique_ptr<juce::Label> octaveRangeHighLabel;

    std::unique_ptr<ThemedCheckbox> freeModeCheckbox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freeModeAttachment;

	std::unique_ptr<ThemedCheckbox> octavesCheckbox;
	std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> octavesAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
