#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

// PreLowPass
// - First-order low-pass controlled by a decay amount mapped to cutoff.

class Lowpass
{
public:
    struct State
    {
        float LPFState = 0.0f; // Low-pass filter state
    };

    // Map decay amount [0..1] to cutoff Hz [18k .. 1k], lower with more decay.
    static inline float AmountToAlpha(float SampleRate, float DecayAmount)
    {
        float LPCutoffHz = juce::jmap(juce::jlimit(0.0f, 1.0f, DecayAmount), 0.0f, 1.0f, 18000.0f, 1000.0f);
        LPCutoffHz = juce::jlimit(100.0f, 20000.0f, LPCutoffHz);

        float Alpha = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * LPCutoffHz / SampleRate);

        return juce::jlimit(0.0f, 1.0f, Alpha);
    }

    // Process one sample through the LP IIR.
    static inline float ProcessSample(Lowpass::State& LPState,
                                      float InputSample,
                                      float AlphaLP)
    {
        LPState.LPFState = LPState.LPFState + AlphaLP * (InputSample - LPState.LPFState);
        return LPState.LPFState;
    }

    static inline void Reset(Lowpass::State& LPState)
    {
        LPState.LPFState = 0.0f;
    }
};