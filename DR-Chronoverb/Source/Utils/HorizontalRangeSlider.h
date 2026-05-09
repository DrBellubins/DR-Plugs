#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "Theme.h"
#include "TooltipOverlay.h"

class HorizontalRangeSlider : public juce::Component,
                              public TooltipClient
{
public:
    enum ActiveThumb
    {
        NoThumb,
        LowerThumb,
        UpperThumb
    };

    HorizontalRangeSlider(float MinimumValue, float MaximumValue);
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
    std::function<void()> OnDragStarted;
    std::function<void()> OnDragEnded;

    void setLowerValue(float NewValue);
    void setUpperValue(float NewValue);

    void setRoundness(float NewRadius);
    void setMinimumRange(float NewMinimumRange);

    void setSteppingEnabled(bool ShouldEnableStepping);
    void setStepSize(float NewStepSize);

    bool isSteppingEnabled() const
    {
        return steppingEnabled;
    }

    float getStepSize() const
    {
        return stepSize;
    }

    bool ShouldShowTooltip() const override;
    juce::Rectangle<float> GetTooltipTargetBoundsInComponent(const juce::Component& TargetComponent) const override;
    juce::String GetTooltipText() const override;
    Placement GetTooltipPlacement() const override;

    ActiveThumb getActiveThumb() const;

    void paint(juce::Graphics& GraphicsContext) override;
    void resized() override;

protected:
    void mouseDown(const juce::MouseEvent& MouseEvent) override;
    void mouseDrag(const juce::MouseEvent& MouseEvent) override;
    void mouseMove(const juce::MouseEvent& MouseEvent) override;
    void mouseExit(const juce::MouseEvent& MouseEvent) override;
    void mouseUp(const juce::MouseEvent& MouseEvent) override;

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

    float minValue = 0.0f;
    float maxValue = 1.0f;
    float lowerValue = 0.0f;
    float upperValue = 1.0f;

    float minimumRange = 0.0f;
    float roundness = 20.0f;

    bool steppingEnabled = false;
    float stepSize = 1.0f;

    DraggingThumb draggingThumb = None;
    HoveredThumb hoveredThumb = HoverNone;

    float thumbWidth = 4.0f;
    float thumbHeight = 25.0f;
    float thumbSideInset = 6.0f;
    float visualMinimumThumbSpacing = 8.0f;

    int dragStartMouseX = 0;
    float dragStartLowerValue = 0.0f;
    float dragStartUpperValue = 0.0f;

    int valueToX(float Value) const;
    float xToValue(int XPosition) const;
    float deltaXToValueDelta(float DeltaX) const;

    juce::Rectangle<float> getRangeRectangle() const;
    juce::Rectangle<float> getLowerThumbRectangle() const;
    juce::Rectangle<float> getUpperThumbRectangle() const;
    juce::Rectangle<float> getActiveThumbRectangle() const;
    HoveredThumb getHoveredThumbAtPosition(juce::Point<int> MousePosition) const;

    float snapValueToStep(float Value) const;
    void notifyTooltipStateChanged();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalRangeSlider)
};

class HorizontalRangeSliderAttachment : private juce::AudioProcessorValueTreeState::Listener
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
    juce::String lowerID;
    juce::String upperID;
    HorizontalRangeSlider& rangeSlider;

    juce::RangedAudioParameter* lowerParameter = nullptr;
    juce::RangedAudioParameter* upperParameter = nullptr;

    bool updatingSlider = false;
    bool updatingParameter = false;
    bool gestureInProgress = false;

    void parameterChanged(const juce::String& ParameterID, float NewValue) override;

    void beginGesture();
    void endGesture();
    void updateSliderFromParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HorizontalRangeSliderAttachment)
};