#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

// DelayLine
// Simple circular buffer delay line supporting push and fractional read by milliseconds.
// Single-channel. Create one per channel.
//
// NOTE: For simplicity, we use linear interpolation for fractional delay reads.
class DelayLine
{
public:
    explicit DelayLine(int maxSamples)
    {
        buffer.resize(std::max(1, maxSamples), 0.0f);
        writeIndex = 0;
    }

    void Clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void PushSample(float inputSample)
    {
        buffer[writeIndex] = inputSample;
        writeIndex = (writeIndex + 1) % buffer.size();
    }

    float ReadDelayMilliseconds(float delayMs, double sampleRate) const
    {
        const double delaySamplesDouble = (delayMs * sampleRate) / 1000.0;
        const int delaySamples = static_cast<int>(std::floor(delaySamplesDouble));
        const float frac = static_cast<float>(delaySamplesDouble - static_cast<double>(delaySamples));

        const int size = static_cast<int>(buffer.size());

        // writeIndex points to the NEXT slot to write, so the last written
        // sample is at (writeIndex - 1). A delay of D samples means we want
        // the sample written D samples ago: (writeIndex - 1) - D.
        int indexA = writeIndex - 1 - delaySamples;
        //int indexA = writeIndex - delaySamples;

        while (indexA < 0)
            indexA += size;

        indexA %= size;

        int indexB = indexA - 1;

        while (indexB < 0)
            indexB += size;

        indexB %= size;

        const float sampleA = buffer[indexA];
        const float sampleB = buffer[indexB];

        return sampleA * (1.0f - frac) + sampleB * frac;
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};