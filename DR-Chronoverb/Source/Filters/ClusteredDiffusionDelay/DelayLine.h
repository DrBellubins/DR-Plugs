#pragma once

#include <vector>
#include <cmath>
#include <juce_core/juce_core.h>

// DelayLine
// - Static utility for circular delay buffer read/write with linear interpolation.
// - State is held externally in DelayLine::State and managed by the caller/master processor.

class DelayLine
{
public:
    struct State
    {
        std::vector<float> Buffer;   // Circular buffer storage
        int WriteIndex = 0;          // Current write index
    };

    // Prepare the delay buffer for a required length in samples.
    // Resets the write index and clears buffer contents.
    static void Prepare(DelayLine::State& DelayState, int MaxDelayBufferSamples)
    {
        DelayState.Buffer.assign(static_cast<size_t>(std::max(1, MaxDelayBufferSamples)), 0.0f);
        DelayState.WriteIndex = 0;
    }

    // Reset the state to silence.
    static void Reset(DelayLine::State& DelayState)
    {
        std::fill(DelayState.Buffer.begin(), DelayState.Buffer.end(), 0.0f);
        DelayState.WriteIndex = 0;
    }

    // Read a sample at DelayInSamples behind the current write index using linear interpolation.
    // Negative delays are clamped to zero.
    static inline float Read(const DelayLine::State& DelayState, float DelayInSamples)
    {
        // Enforce non-negative delay
        if (DelayInSamples < 0.0f)
            DelayInSamples = 0.0f;

        const int BufferSize = static_cast<int>(DelayState.Buffer.size());

        if (BufferSize <= 1)
            return 0.0f;

        // Compute read position relative to the write index
        float ReadPosition = static_cast<float>(DelayState.WriteIndex) - DelayInSamples;

        // Wrap into the circular range [0 .. BufferSize)
        while (ReadPosition < 0.0f)
            ReadPosition += static_cast<float>(BufferSize);

        // Linear interpolation
        int IndexA = static_cast<int>(ReadPosition) % BufferSize;
        int IndexB = (IndexA + 1) % BufferSize;
        float Fraction = ReadPosition - static_cast<float>(IndexA);

        const float SampleA = DelayState.Buffer[static_cast<size_t>(IndexA)];
        const float SampleB = DelayState.Buffer[static_cast<size_t>(IndexB)];

        return SampleA + (SampleB - SampleA) * Fraction;
    }

    // Write a sample into the circular buffer and advance the write index (with wrap).
    static inline void Write(DelayLine::State& DelayState, float Sample)
    {
        const int BufferSize = static_cast<int>(DelayState.Buffer.size());

        if (BufferSize <= 0)
            return;

        DelayState.Buffer[DelayState.WriteIndex] = Sample;

        DelayState.WriteIndex++;

        if (DelayState.WriteIndex >= BufferSize)
            DelayState.WriteIndex = 0;
    }
};