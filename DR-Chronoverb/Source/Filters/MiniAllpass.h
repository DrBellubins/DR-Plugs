#pragma once

#include <vector>

// Add forward declaration for mini all-pass helper (inline implementation for brevity).
class MiniAllpass
{
public:
    void prepare(int DelaySamples)
    {
        buffer.assign(static_cast<size_t>(std::max(1, DelaySamples)), 0.0f);
        writeIndex = 0;
    }

    void reset()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
        z = 0.0f;
    }

    void setCoefficient(float NewCoefficient)
    {
        coefficient = juce::jlimit(-0.85f, 0.85f, NewCoefficient);
    }

    float processSample(float InputSample)
    {
        const int bufferSize = static_cast<int>(buffer.size());

        if (bufferSize == 0)
            return InputSample;

        float delayed = buffer[static_cast<size_t>(writeIndex)];
        float outputSample = -coefficient * InputSample + delayed + coefficient * z;
        buffer[static_cast<size_t>(writeIndex)] = InputSample + coefficient * outputSample;

        writeIndex = (writeIndex + 1) % bufferSize;
        z = outputSample;
        return outputSample;
    }

private:
    std::vector<float> buffer;
    int writeIndex = 0;
    float coefficient = 0.72f;
    float z = 0.0f;
};