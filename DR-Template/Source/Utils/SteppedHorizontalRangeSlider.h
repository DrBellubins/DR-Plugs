#pragma once

#include "HorizontalRangeSlider.h"

class SteppedHorizontalRangeSlider : public HorizontalRangeSlider
{
public:
	SteppedHorizontalRangeSlider(float MinimumValue, float MaximumValue, float StepSize);

	void setStepSize(float NewStepSize);
	float getStepSize() const;

	void setLowerValue(float NewValue) override;
	void setUpperValue(float NewValue) override;

protected:
	void mouseDrag(const juce::MouseEvent& MouseEvent) override;

private:
	float stepSize = 12.0f; // Default: 12 (for octaves if pitch is MIDI)

	float quantizeToStep(float Value) const;

	float getMinValue() const;
	float getMaxValue() const;
};