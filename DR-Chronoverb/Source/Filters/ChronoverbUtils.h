#pragma once

#include <juce_dsp/juce_dsp.h>

inline float clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

inline float map01ToRange(float value01, float minValue, float maxValue)
{
    return minValue + (maxValue - minValue) * clamp01(value01);
}

inline int clampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}

inline int semitonesToOctaveIndex(float semitones)
{
    return static_cast<int>(std::round(semitones / 12.0f));
}

inline std::pair<float, float> GetDelayReverbGain(float diffusionAmount)
{
    const float delayReverbBlend = (diffusionAmount - 0.5f) * 2.0f;

    float delayGain = std::cos(delayReverbBlend * juce::MathConstants<float>::halfPi);
    float reverbGain = std::sin(delayReverbBlend * juce::MathConstants<float>::halfPi);

    // Suppress floating-point noise near zero
    if (std::abs(delayGain) < 1.0e-6f) delayGain = 0.0f;
    if (std::abs(reverbGain) < 1.0e-6f) reverbGain = 0.0f;

    delayGain = clamp01(delayGain);
    reverbGain = clamp01(reverbGain);

    return std::make_pair(delayGain, reverbGain);
}