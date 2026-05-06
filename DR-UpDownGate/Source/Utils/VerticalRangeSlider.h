#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class VerticalRangeSlider : public juce::Component
{
public:
    VerticalRangeSlider(float minimumValue, float maximumValue);
    ~VerticalRangeSlider() override = default;

    float getLowerValue() const
    {
        return lowerValue;
    }

    float getUpperValue() const
    {
        return upperValue;
    }

    std::function<void(float)> OnLowerValueChanged;
    std::function<void(float)> OnUpperValueChanged;

    void setLowerValue(float newValue);
    void setUpperValue(float newValue);

    void setRoundness(float newRadius);
    void setMinimumRange(float newMinimumRange);

    void paint(juce::Graphics& graphics) override;
    void resized() override;

protected:
    void mouseDown(const juce::MouseEvent& mouseEvent) override;
    void mouseDrag(const juce::MouseEvent& mouseEvent) override;
    void mouseMove(const juce::MouseEvent& mouseEvent) override;
    void mouseExit(const juce::MouseEvent& mouseEvent) override;
    void mouseUp(const juce::MouseEvent& mouseEvent) override;

private:
    enum DraggingThumb
    {
        None,
        Lower,
        Upper
    };

    enum HoveredThumb
    {
        HoverNone,
        HoverLower,
        HoverUpper
    };

    float minValue;
    float maxValue;
    float lowerValue;
    float upperValue;

    float minimumRange = 0.25f;
    float roundness = 20.0f;

    DraggingThumb draggingThumb = None;
    HoveredThumb hoveredThumb = HoverNone;

    float thumbWidth = 18.0f;
    float thumbHeight = 4.0f;
    float thumbTopInset = 6.0f;
    float visualMinimumThumbSpacing = 8.0f;

    int valueToY(float value) const;
    float yToValue(int yPosition) const;

    juce::Rectangle<float> getRangeRectangle() const;
    juce::Rectangle<float> getUpperThumbRectangle() const;
    juce::Rectangle<float> getLowerThumbRectangle() const;
    HoveredThumb getHoveredThumbAtPosition(juce::Point<int> mousePosition) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerticalRangeSlider)
};