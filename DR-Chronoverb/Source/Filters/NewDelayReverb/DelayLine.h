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
        const double delaySamples = (delayMs * sampleRate) / 1000.0;
        const double readPos = static_cast<double>(writeIndex) - delaySamples - 1.0;

        const int size = static_cast<int>(buffer.size());

        double wrappedReadPos = readPos;
        while (wrappedReadPos < 0.0)
            wrappedReadPos += static_cast<double>(size);
        while (wrappedReadPos >= static_cast<double>(size))
            wrappedReadPos -= static_cast<double>(size);

        const int indexA = static_cast<int>(std::floor(wrappedReadPos));
        const int indexB = (indexA + 1) % size;
        const float frac = static_cast<float>(wrappedReadPos - static_cast<double>(indexA));

        const float sampleA = buffer[indexA];
        const float sampleB = buffer[indexB];

        return sampleA + (sampleB - sampleA) * frac;
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
};