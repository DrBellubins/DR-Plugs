#pragma once

#include <juce_dsp/juce_dsp.h>

#include "NewDelayReverb/DeverbDiffusionChain.h"

inline float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
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
    const float delayReverbBlend = std::clamp((diffusionAmount - 0.5f) * 2.0f, 0.0f, 1.0f);

    float delayGain = std::cos(delayReverbBlend * juce::MathConstants<float>::halfPi);
    float reverbGain = std::sin(delayReverbBlend * juce::MathConstants<float>::halfPi);

    // Suppress floating-point noise near zero
    if (std::abs(delayGain) < 1.0e-6f) delayGain = 0.0f;
    if (std::abs(reverbGain) < 1.0e-6f) reverbGain = 0.0f;

    delayGain = clamp01(delayGain);
    reverbGain = clamp01(reverbGain);

    return std::make_pair(delayGain, reverbGain);
}

inline std::pair<float, float> GetDelayDiffusedTapGain(float diffusionAmount,
                                                        float crossfadeSpeed = 1.0f)
{
    const float driven = std::clamp(diffusionAmount * crossfadeSpeed, 0.0f, 1.0f);

    const float nominalTapGain  = std::pow(1.0f - driven, 4.0f);
    const float diffusedTapGain = std::sin(driven * juce::MathConstants<float>::halfPi);

    const float euclideanNorm = std::sqrt(nominalTapGain * nominalTapGain
                                        + diffusedTapGain * diffusedTapGain);
    const float normFactor = (euclideanNorm > 1.0e-6f) ? (1.0f / euclideanNorm) : 1.0f;

    return std::make_pair(nominalTapGain * normFactor, diffusedTapGain * normFactor);
}

inline bool IsPrime(int n)
{
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;

    for (int i = 3; i * i <= n; i += 2)
        if (n % i == 0) return false;

    return true;
}

// Generates a decorrelated copy of tunings by nudging each value to a nearby prime
// within maxShiftPercent of the original. Prefers upward shifts for consistent
// decorrelation. Falls back to nearest prime in either direction if needed.
inline std::vector<float> DecorrelateTunings(const std::vector<float>& tunings, float maxShiftPercent = 0.10f)
{
    std::vector<float> result;
    result.reserve(tunings.size());

    for (float t : tunings)
    {
        const int base = static_cast<int>(std::round(t));
        const int maxDelta = std::max(2, static_cast<int>(std::round(base * maxShiftPercent)));

        int best = -1;

        // Prefer upward shift for consistent channel decorrelation
        for (int delta = 1; delta <= maxDelta; ++delta)
        {
            if (IsPrime(base + delta))
            {
                best = base + delta;
                break;
            }
        }

        // Fallback: search both directions if no upward prime found in range
        if (best < 0)
        {
            for (int delta = 1; delta <= maxDelta * 2; ++delta)
            {
                if (IsPrime(base + delta)) { best = base + delta; break; }
                if (base - delta >= 2 && IsPrime(base - delta)) { best = base - delta; break; }
            }
        }

        // Last resort: keep original (should never happen with reasonable inputs)
        result.push_back(static_cast<float>(best >= 2 ? best : base));
    }

    return result;
}

inline std::array<float, DeverbDiffusionChain::MaxStages>
DecorrelateTunings(const std::array<float, DeverbDiffusionChain::MaxStages>& tunings,
                   float maxShiftPercent = 0.10f)
{
    std::array<float, DeverbDiffusionChain::MaxStages> result {};

    for (size_t i = 0; i < DeverbDiffusionChain::MaxStages; ++i)
    {
        const float t = tunings[i];
        const int base = static_cast<int>(std::round(t));
        const int maxDelta = std::max(2, static_cast<int>(std::round(base * maxShiftPercent)));

        int best = -1;

        // Prefer upward shift for consistent channel decorrelation
        for (int delta = 1; delta <= maxDelta; ++delta)
        {
            if (IsPrime(base + delta))
            {
                best = base + delta;
                break;
            }
        }

        // Fallback: search both directions if no upward prime found in range
        if (best < 0)
        {
            for (int delta = 1; delta <= maxDelta * 2; ++delta)
            {
                if (IsPrime(base + delta))
                {
                    best = base + delta;
                    break;
                }

                if (base - delta >= 2 && IsPrime(base - delta))
                {
                    best = base - delta;
                    break;
                }
            }
        }

        // Last resort: keep original
        result[i] = static_cast<float>(best >= 2 ? best : base);
    }

    return result;
}