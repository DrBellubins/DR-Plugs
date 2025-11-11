#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "HorizontalRangeSlider.h"

// A custom attachment that binds two AudioProcessorValueTreeState parameters to a HorizontalRangeSlider.
class HorizontalRangeSliderAttachment :
	private juce::AudioProcessorValueTreeState::Listener
{
public:
	HorizontalRangeSliderAttachment(
		juce::AudioProcessorValueTreeState& ParameterValueTreeState,
		const juce::String& LowerParameterID,
		const juce::String& UpperParameterID,
		HorizontalRangeSlider& RangeSlider);

	~HorizontalRangeSliderAttachment() override;

private:
	juce::AudioProcessorValueTreeState& valueTreeState;
	juce::String lowerID, upperID;
	HorizontalRangeSlider& rangeSlider;

	bool updatingSlider = false;
	bool updatingParameter = false;

	// Called when parameter changes (from DAW, automation, etc.)
	void parameterChanged(const juce::String& ParameterID, float NewValue) override;

	void updateSliderFromParameters();
};