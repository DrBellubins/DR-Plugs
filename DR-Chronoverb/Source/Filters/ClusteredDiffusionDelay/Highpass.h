#pragma once

#include <cmath>
#include <juce_core/juce_core.h>

// PreHighPass
// - First-order high-pass realized as x - lowpass(x).
// - Controlled by a decay amount mapped to cutoff.

class Highpass
{
public:
    struct State
    {
        float LPFState = 0.0f; // Low-pass state used to realize HP as (x - LPFState)
    };

    // Map decay amount [0..1] to cutoff Hz [20 .. 2k], higher with more decay.
    static inline float AmountToAlpha(float SampleRate, float DecayAmount)
    {
        float HPCutoffHz = juce::jmap(juce::jlimit(0.0f, 1.0f, DecayAmount), 0.0f, 1.0f, 20.0f, 2000.0f);
        HPCutoffHz = juce::jlimit(10.0f, 8000.0f, HPCutoffHz);

        float Alpha = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * HPCutoffHz / SampleRate);

        return juce::jlimit(0.0f, 1.0f, Alpha);
    }

    // Process one sample through the HP IIR (as x - lpf(x)).
    static inline float ProcessSample(Highpass::State& HPState,
                                      float InputSample,
                                      float AlphaHP)
    {
        HPState.LPFState = HPState.LPFState + AlphaHP * (InputSample - HPState.LPFState);
        float HPOutput = InputSample - HPState.LPFState;

        return HPOutput;
    }

    static inline void Reset(Highpass::State& HPState)
    {
        HPState.LPFState = 0.0f;
    }
};