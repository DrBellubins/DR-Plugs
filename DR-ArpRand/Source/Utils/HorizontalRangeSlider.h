#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// HorizontalRangeSlider: Dual-value slider for a horizontal value range.
class HorizontalRangeSlider : public juce::Component
{
public:
	float minValue, maxValue;
	float lowerValue, upperValue;

	HorizontalRangeSlider(float MinimumValue, float MaximumValue);
	~HorizontalRangeSlider() override = default;

	float getLowerValue() const { return lowerValue; }
	float getUpperValue() const { return upperValue; }

	std::function<void(float)> OnLowerValueChanged;
	std::function<void(float)> OnUpperValueChanged;

	virtual void setLowerValue(float NewValue);
	virtual void setUpperValue(float NewValue);

	void setRoundness(float Radius);

	void paint(juce::Graphics& Graphics) override;
	void resized() override;

protected:
	enum DraggingThumb { None, Lower, Upper };
	DraggingThumb dragging = None;

	int valueToX(float Value) const;
	float xToValue(int X) const;

	void mouseDown(const juce::MouseEvent& MouseEvent) override;
	void mouseDrag(const juce::MouseEvent& MouseEvent) override;
	void mouseMove(const juce::MouseEvent& MouseEvent) override;

private:
	float roundness = 20.0f; // Default roundness

	int handleThickness = 4; // Thickness of the drag handles
	int handleMargin = 8;    // Margin inside the range rect for handles

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalRangeSlider)
};