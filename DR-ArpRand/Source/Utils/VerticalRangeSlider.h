#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class VerticalRangeSlider : public juce::Component
{
public:
    VerticalRangeSlider(float min, float max);
    ~VerticalRangeSlider() override = default;

    float getLowerValue() const { return lowerValue; }
    float getUpperValue() const { return upperValue; }

    std::function<void(float)> OnLowerValueChanged;
    std::function<void(float)> OnUpperValueChanged;

    void setLowerValue(float value);
    void setUpperValue(float value);

    void setRoundness(float radius);

    void paint(juce::Graphics&) override;
    void resized() override;

protected:
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    float minValue, maxValue;
    float lowerValue, upperValue;

    float roundness = 20.0f; // default roundness

    enum DraggingThumb { None, Lower, Upper };
    DraggingThumb dragging = None;

    int handleThickness = 4; // thickness of the drag handles
    int handleMargin = 8;    // margin inside the range rect for handles

    int valueToY(float value) const;
    float yToValue(int y) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerticalRangeSlider)
};