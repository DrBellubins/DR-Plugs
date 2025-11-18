#pragma once

#include <vector>
#include <cmath>
#include <juce_core/juce_core.h>

// HaasStereoWidener
// - For StereoWidth <= 0: mid/side reduction towards mono.
// - For StereoWidth >  0: Haas widening by delaying the right channel using a fractional delay buffer.
// - Per-channel Haas state is maintained externally.

class HaasStereoWidener
{
public:
    struct State
    {
        std::vector<float> Buffer;   // Circular buffer for Haas delay
        int WriteIndex = 0;          // Write pointer
        int MaxDelaySamples = 1;     // Maximum delay in samples for Haas
    };

    // Prepare a Haas buffer of the requested maximum fractional delay (in samples).
    static void Prepare(HaasStereoWidener::State& HaasState, int MaxDelaySamples)
    {
        HaasState.MaxDelaySamples = std::max(1, MaxDelaySamples);
        HaasState.Buffer.assign(static_cast<size_t>(HaasState.MaxDelaySamples + 1), 0.0f);
        HaasState.WriteIndex = 0;
    }

    // Reset buffer and indices.
    static void Reset(HaasStereoWidener::State& HaasState)
    {
        std::fill(HaasState.Buffer.begin(), HaasState.Buffer.end(), 0.0f);
        HaasState.WriteIndex = 0;
    }

    // Write current wet samples to Haas buffers before reading delayed values.
    static inline void WriteWet(HaasStereoWidener::State& HaasState, float WetSample)
    {
        HaasState.Buffer[static_cast<size_t>(HaasState.WriteIndex)] = WetSample;
    }

    // Advance write indices (wrap).
    static inline void Advance(HaasStereoWidener::State& HaasState)
    {
        HaasState.WriteIndex++;

        if (HaasState.WriteIndex >= static_cast<int>(HaasState.Buffer.size()))
            HaasState.WriteIndex = 0;
    }

    // Fractional read from Haas buffer with linear interpolation.
    static inline float Read(const HaasStereoWidener::State& HaasState, float DelayInSamples)
    {
        const int BufferSize = static_cast<int>(HaasState.Buffer.size());

        if (BufferSize <= 1)
            return 0.0f;

        // Read position is WriteIndex - DelayInSamples
        float ReadPosition = static_cast<float>(HaasState.WriteIndex) - DelayInSamples;

        while (ReadPosition < 0.0f)
            ReadPosition += static_cast<float>(BufferSize);

        int IndexA = static_cast<int>(ReadPosition) % BufferSize;
        int IndexB = (IndexA + 1) % BufferSize;
        float Fraction = ReadPosition - static_cast<float>(IndexA);

        const float SampleA = HaasState.Buffer[static_cast<size_t>(IndexA)];
        const float SampleB = HaasState.Buffer[static_cast<size_t>(IndexB)];

        return SampleA + (SampleB - SampleA) * Fraction;
    }

    // Process one stereo wet sample:
    // - StereoWidth <= 0: compress side using mid/side scale.
    // - StereoWidth  > 0: delay right channel up to MaxDelaySamples with fractional interpolation.
    static inline void ProcessStereoSample(float InputWetLeft,
                                           float InputWetRight,
                                           float StereoWidth,
                                           HaasStereoWidener::State& LeftHaasState,
                                           HaasStereoWidener::State& RightHaasState,
                                           float& OutWetLeft,
                                           float& OutWetRight)
    {
        if (StereoWidth <= 0.0f)
        {
            // Mid/Side reduction: sideScale in [0..1] for width in [-1..0]
            const float Mid = 0.5f * (InputWetLeft + InputWetRight);
            const float Side = 0.5f * (InputWetLeft - InputWetRight);

            const float SideScale = 1.0f + StereoWidth;

            OutWetLeft = Mid + Side * SideScale;
            OutWetRight = Mid - Side * SideScale;

            // Keep Haas buffers consistent (still write and advance)
            WriteWet(LeftHaasState, InputWetLeft);
            WriteWet(RightHaasState, InputWetRight);
            Advance(LeftHaasState);
            Advance(RightHaasState);
        }
        else
        {
            // Haas widening: delay the right channel by up to MaxDelaySamples - 1
            const float MaxSamplesF = static_cast<float>(std::max(1, RightHaasState.MaxDelaySamples));
            const float HaasDelaySamples = StereoWidth * (MaxSamplesF - 1.0f);

            // Write current wet to buffers first
            WriteWet(LeftHaasState, InputWetLeft);
            WriteWet(RightHaasState, InputWetRight);

            // Left un-delayed, Right delayed
            OutWetLeft = InputWetLeft;
            OutWetRight = HaasStereoWidener::Read(RightHaasState, HaasDelaySamples);

            // Advance indices after read
            Advance(LeftHaasState);
            Advance(RightHaasState);
        }
    }
};