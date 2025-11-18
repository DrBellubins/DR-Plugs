#pragma once

#include <juce_core/juce_core.h>

// DryWetMixer
// - Equal-power gains for dry/wet mixing using sin/cos law.

class DryWetMixer
{
public:
    // Compute equal-power crossfade gains for DryWet in [0..1].
    static inline void ComputeGains(float DryWetMix,
                                    float& OutDryGain,
                                    float& OutWetGain)
    {
        OutDryGain = std::cos(DryWetMix * juce::MathConstants<float>::halfPi);
        OutWetGain = std::sin(DryWetMix * juce::MathConstants<float>::halfPi);
    }
};