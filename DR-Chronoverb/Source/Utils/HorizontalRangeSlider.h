#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class HorizontalRangeSlider : public juce::Component
{
public:
    enum ActiveThumb
    {
        NoThumb,
        LowerThumb,
        UpperThumb
    };

    HorizontalRangeSlider(float minimumValue, float maximumValue);
    ~HorizontalRangeSlider() override = default;

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
    std::function<void()> OnTooltipStateChanged;

    void setLowerValue(float newValue);
    void setUpperValue(float newValue);

    void setRoundness(float newRadius);
    void setMinimumRange(float newMinimumRange);

    void setStepSize(float newStepSize);
    void setSteppingEnabled(bool shouldEnableStepping);

    bool shouldShowTooltip() const;
    ActiveThumb getActiveThumb() const;
    juce::Rectangle<float> getActiveThumbBoundsInComponent(const juce::Component& targetComponent) const;
    juce::String getActiveThumbTooltipText() const;

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

    float minimumRange = 0.0f;
    float roundness = 20.0f;

    bool steppingEnabled = false;
    float stepSize = 1.0f;

    DraggingThumb draggingThumb = None;
    HoveredThumb hoveredThumb = HoverNone;

    float thumbWidth = 4.0f;
    float thumbHeight = 15.0f;
    float thumbSideInset = 12.0f;
    float visualMinimumThumbSpacing = 8.0f;

    int dragStartMouseX = 0;
    float dragStartLowerValue = 0.0f;
    float dragStartUpperValue = 0.0f;

    int valueToX(float value) const;
    float xToValue(int xPosition) const;
    float deltaXToValueDelta(float deltaX) const;

    juce::Rectangle<float> getRangeRectangle() const;
    juce::Rectangle<float> getLowerThumbRectangle() const;
    juce::Rectangle<float> getUpperThumbRectangle() const;
    juce::Rectangle<float> getActiveThumbRectangle() const;
    HoveredThumb getHoveredThumbAtPosition(juce::Point<int> mousePosition) const;

    float snapValueToStep(float value) const;
    void notifyTooltipStateChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalRangeSlider)
};

class HorizontalRangeSliderAttachment : private juce::AudioProcessorValueTreeState::Listener
{
public:
    HorizontalRangeSliderAttachment(
        juce::AudioProcessorValueTreeState& parameterValueTreeState,
        const juce::String& lowerParameterID,
        const juce::String& upperParameterID,
        HorizontalRangeSlider& rangeSlider);

    ~HorizontalRangeSliderAttachment() override;

private:
    juce::AudioProcessorValueTreeState& valueTreeState;
    juce::String lowerID;
    juce::String upperID;
    HorizontalRangeSlider& rangeSlider;

    bool updatingSlider = false;
    bool updatingParameter = false;

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void updateSliderFromParameters();
};