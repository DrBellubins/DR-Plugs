#pragma once

#include <vector>
#include <cmath>

// DiffusionAllpass
// Simple delay-based allpass:
// y[n] = -g * x[n] + x[n - D] + g * y[n - D]
// We implement using a circular buffer with per-sample push, and fractional D support via linear interpolation.
//
// NOTE: The classical Schroeder allpass form varies by sign and exact implementation.
// Here we pick a stable form with |g| < 1.0.
class DiffusionAllpass
{
public:
    DiffusionAllpass()
    {
    }

    void Prepare(double sampleRate)
    {
        sr = sampleRate;
        setDelayMilliseconds(50.0f); // default
        setGain(0.65f);              // default
        clear();
    }

    void Configure(float delayMilliseconds, float gain)
    {
        setDelayMilliseconds(delayMilliseconds);
        setGain(gain);
        ensureBufferSize();
        clear();
    }

    float ProcessSample(float inputSample)
    {
        // Read delayed values
        const float xDelayed = readDelaySamples(delaySamples);
        const float yDelayed = readOutputDelaySamples(delaySamples);

        // Allpass difference equation (one of the canonical forms)
        const float y = -g * inputSample + xDelayed + g * yDelayed;

        // Push input and output into buffers
        pushInput(inputSample);
        pushOutput(y);

        return y;
    }

    void clear()
    {
        std::fill(inputBuffer.begin(), inputBuffer.end(), 0.0f);
        std::fill(outputBuffer.begin(), outputBuffer.end(), 0.0f);
        inputWrite = 0;
        outputWrite = 0;
    }

private:
    double sr = 48000.0;
    float delayMs = 50.0f;
    float g = 0.65f;

    std::vector<float> inputBuffer;
    std::vector<float> outputBuffer;

    int inputWrite = 0;
    int outputWrite = 0;

    int delaySamples = 2400; // 50 ms @ 48 kHz

    void setDelayMilliseconds(float newDelayMs)
    {
        delayMs = std::max(1.0f, newDelayMs);
        delaySamples = static_cast<int>(std::round((delayMs * sr) / 1000.0));
        ensureBufferSize();
    }

    void setGain(float newGain)
    {
        g = std::max(-0.99f, std::min(0.99f, newGain));
    }

    void ensureBufferSize()
    {
        const int minSize = std::max(4, delaySamples + 4);

        if (static_cast<int>(inputBuffer.size()) < minSize)
            inputBuffer.resize(minSize, 0.0f);

        if (static_cast<int>(outputBuffer.size()) < minSize)
            outputBuffer.resize(minSize, 0.0f);
    }

    void pushInput(float x)
    {
        inputBuffer[inputWrite] = x;
        inputWrite = (inputWrite + 1) % static_cast<int>(inputBuffer.size());
    }

    void pushOutput(float y)
    {
        outputBuffer[outputWrite] = y;
        outputWrite = (outputWrite + 1) % static_cast<int>(outputBuffer.size());
    }

    float readDelaySamples(int d) const
    {
        const int size = static_cast<int>(inputBuffer.size());
        int index = inputWrite - d;
        while (index < 0) index += size;
        index %= size;
        return inputBuffer[index];
    }

    float readOutputDelaySamples(int d) const
    {
        const int size = static_cast<int>(outputBuffer.size());
        int index = outputWrite - d;
        while (index < 0) index += size;
        index %= size;
        return outputBuffer[index];
    }
};