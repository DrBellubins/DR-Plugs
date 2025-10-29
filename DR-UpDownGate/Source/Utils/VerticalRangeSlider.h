#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class VerticalRangeSlider : public juce::Component
{
public:
    VerticalRangeSlider(float min, float max);
    ~VerticalRangeSlider() override = default;

    float getLowerValue() const { return lowerValue; }
    float getUpperValue() const { return upperValue; }

    void setLowerValue(float value);
    void setUpperValue(float value);

    // Optionally add: onChange callback or listener

    void paint(juce::Graphics&) override;
    void resized() override;

protected:
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    float minValue, maxValue;
    float lowerValue, upperValue;

    enum DraggingThumb { None, Lower, Upper };
    DraggingThumb dragging = None;

    int thumbRadius = 8;

    // Maps a value to a Y position and vice versa
    int valueToY(float value) const;
    float yToValue(int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerticalRangeSlider)
};