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