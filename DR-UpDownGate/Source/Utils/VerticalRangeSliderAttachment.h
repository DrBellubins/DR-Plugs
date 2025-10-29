#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "VerticalRangeSlider.h"

// A custom attachment that binds two AudioProcessorValueTreeState parameters to a VerticalRangeSlider.
class VerticalRangeSliderAttachment :
    private juce::AudioProcessorValueTreeState::Listener,
    private juce::Timer
{
public:
    VerticalRangeSliderAttachment(
        juce::AudioProcessorValueTreeState& ParameterValueTreeState,
        const juce::String& LowerParameterID,
        const juce::String& UpperParameterID,
        VerticalRangeSlider& RangeSlider)
        : valueTreeState(ParameterValueTreeState),
          lowerID(LowerParameterID),
          upperID(UpperParameterID),
          rangeSlider(RangeSlider)
    {
        // Listen for parameter changes
        valueTreeState.addParameterListener(lowerID, this);
        valueTreeState.addParameterListener(upperID, this);

        // Set initial slider values from parameters
        updateSliderFromParameters();

        // Listen for slider changes using a timer, or you can add custom listeners
        startTimerHz(30); // Check for UI changes at 30 Hz
    }

    ~VerticalRangeSliderAttachment() override
    {
        valueTreeState.removeParameterListener(lowerID, this);
        valueTreeState.removeParameterListener(upperID, this);
        stopTimer();
    }

private:
    juce::AudioProcessorValueTreeState& valueTreeState;
    juce::String lowerID, upperID;
    VerticalRangeSlider& rangeSlider;

    bool updatingSlider = false;
    bool updatingParameter = false;

    // Called when parameter changes (from DAW, automation, etc.)
    void parameterChanged(const juce::String& ParameterID, float NewValue) override
    {
        juce::MessageManagerLock lock; // UI thread required
        if (! updatingParameter)
        {
            updatingSlider = true;
            updateSliderFromParameters();
            updatingSlider = false;
        }
    }

    // Poll the slider for changes (since it doesn't have built-in listeners)
    void timerCallback() override
    {
        if (updatingSlider) return; // Prevent feedback loop

        static float lastLower = -1000.0f, lastUpper = -1000.0f;
        float currentLower = rangeSlider.getLowerValue();
        float currentUpper = rangeSlider.getUpperValue();

        if (currentLower != lastLower)
        {
            updatingParameter = true;
            *valueTreeState.getRawParameterValue(lowerID) = currentLower;
            updatingParameter = false;
            lastLower = currentLower;
        }
        if (currentUpper != lastUpper)
        {
            updatingParameter = true;
            *valueTreeState.getRawParameterValue(upperID) = currentUpper;
            updatingParameter = false;
            lastUpper = currentUpper;
        }
    }

    void updateSliderFromParameters()
    {
        float lower = valueTreeState.getRawParameterValue(lowerID)->load();
        float upper = valueTreeState.getRawParameterValue(upperID)->load();
        rangeSlider.setLowerValue(lower);
        rangeSlider.setUpperValue(upper);
    }
};