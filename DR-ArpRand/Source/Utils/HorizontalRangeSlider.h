#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// HorizontalRangeSlider: Dual-value slider for a horizontal value range.
class HorizontalRangeSlider : public juce::Component
{
public:
	HorizontalRangeSlider(float MinimumValue, float MaximumValue);
	~HorizontalRangeSlider() override = default;

	float getLowerValue() const { return lowerValue; }
	float getUpperValue() const { return upperValue; }

	std::function<void(float)> OnLowerValueChanged;
	std::function<void(float)> OnUpperValueChanged;

	void setLowerValue(float NewValue);
	void setUpperValue(float NewValue);

	void setRoundness(float Radius);

	void paint(juce::Graphics& Graphics) override;
	void resized() override;

protected:
	void mouseDown(const juce::MouseEvent& MouseEvent) override;
	void mouseDrag(const juce::MouseEvent& MouseEvent) override;
	void mouseMove(const juce::MouseEvent& MouseEvent) override;

private:
	float minValue, maxValue;
	float lowerValue, upperValue;

	float roundness = 20.0f; // Default roundness

	enum DraggingThumb { None, Lower, Upper };
	DraggingThumb dragging = None;

	int handleThickness = 4; // Thickness of the drag handles
	int handleMargin = 8;    // Margin inside the range rect for handles

	int valueToX(float Value) const;
	float xToValue(int X) const;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalRangeSlider)
};