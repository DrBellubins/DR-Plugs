#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

// FeedbackDamping
// - One-pole low-pass damping in the feedback path.
// - Maps DiffusionAmount and DiffusionQuality to a perceptual cutoff -> alpha coefficient.
// - Converts T60 to per-loop feedback gain.

class FeedbackDamping
{
public:
    struct State
    {
        float OnePoleState = 0.0f; // State of the one-pole low-pass filter
    };

    // Compute damping alpha from a cutoff derived from amount and quality.
    static inline float ComputeDampingAlpha(float SampleRate,
                                            float DiffusionAmount,
                                            float DiffusionQuality)
    {
        // Map amount and quality to a cutoff [3 kHz .. 12 kHz], lower with higher diffusion/quality.
        float CutoffHz = juce::jmap(juce::jlimit(0.0f, 1.0f, DiffusionAmount), 0.0f, 1.0f, 12000.0f, 6000.0f);
        CutoffHz = juce::jmap(juce::jlimit(0.0f, 1.0f, DiffusionQuality), 0.0f, 1.0f, CutoffHz, CutoffHz * 0.8f);
        CutoffHz = juce::jlimit(1000.0f, 18000.0f, CutoffHz);

        float Alpha = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * CutoffHz / SampleRate);
        Alpha = juce::jlimit(0.0f, 1.0f, Alpha);

        return Alpha;
    }

    // Convert T60 to per-loop feedback gain. T60Seconds <= 0 disables feedback.
    static inline float T60ToFeedbackGain(float LoopSeconds, float T60Seconds)
    {
        if (T60Seconds <= 0.0f || LoopSeconds <= 0.0f)
            return 0.0f;

        float Gain = std::pow(10.0f, -3.0f * (LoopSeconds / T60Seconds));
        return juce::jlimit(0.0f, 0.9995f, Gain);
    }

    // Process one sample through the damping filter and apply feedback gain.
    static inline float ProcessSample(FeedbackDamping::State& DampingState,
                                      float InputWetSample,
                                      float DampingAlpha,
                                      float FeedbackGain)
    {
        // One-pole low-pass on wet input to generate feedback tone shaping
        DampingState.OnePoleState = DampingState.OnePoleState + DampingAlpha * (InputWetSample - DampingState.OnePoleState);
        const float FeedbackSample = DampingState.OnePoleState * FeedbackGain;

        return FeedbackSample;
    }

    // Reset the damping state.
    static inline void Reset(FeedbackDamping::State& DampingState)
    {
        DampingState.OnePoleState = 0.0f;
    }
};