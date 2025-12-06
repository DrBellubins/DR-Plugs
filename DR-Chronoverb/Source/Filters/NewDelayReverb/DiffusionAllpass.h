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
        SetDelayMilliseconds(50.0f); // default
        setGain(0.65f);              // default
        Clear();
    }

    void Configure(float delayMilliseconds, float gain)
    {
        SetDelayMilliseconds(delayMilliseconds);
        setGain(gain);
        ensureBufferSize();
        Clear();
    }

    float ProcessSample(float inputSample)
    {
        // Read delayed values using current fractional delay
        const float xDelayed = readDelaySamplesFractional(currentDelaySamples);
        const float yDelayed = readOutputDelaySamplesFractional(currentDelaySamples);

        // Allpass equation
        const float y = -g * inputSample + xDelayed + g * yDelayed;

        // Push input and output
        pushInput(inputSample);
        pushOutput(y);

        return y;
    }

    // Set a base delay in milliseconds once (Configure calls this).
    void SetBaseDelayMilliseconds(float newDelayMs)
    {
        delayMs = std::max(1.0f, newDelayMs);
        delaySamplesInteger = static_cast<int>(std::floor((delayMs * sr) / 1000.0));
        ensureBufferSize();
        currentDelaySamples = static_cast<float>(delaySamplesInteger);
    }

    // Update the current fractional delay in samples without reallocating/clearing buffers.
    // Use this per-sample for jitter modulation.
    void SetCurrentDelaySamples(float newDelaySamples)
    {
        // Slew-limit to avoid zipper noise in fractional interpolation
        const float maxDelta = 0.25f; // samples per call (adjust if needed)
        const float delta = juce::jlimit(-maxDelta, maxDelta, newDelaySamples - currentDelaySamples);
        currentDelaySamples += delta;

        // Bound between 1 and buffer size - 3
        const float minD = 1.0f;
        const float maxD = static_cast<float>(std::max(4, static_cast<int>(inputBuffer.size()) - 3));
        currentDelaySamples = juce::jlimit(minD, maxD, currentDelaySamples);
    }

    void SetDelayMilliseconds(float newDelayMs)
    {
        delayMs = std::max(1.0f, newDelayMs);
        delaySamples = static_cast<int>(std::round((delayMs * sr) / 1000.0));
        ensureBufferSize();
    }

    void Clear()
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

    int delaySamples = 2400; // keep legacy path if needed
    int delaySamplesInteger = 2400;
    float currentDelaySamples = 2400.0f;

    float readDelaySamplesFractional(float delaySamplesFloat) const
    {
        const int size = static_cast<int>(inputBuffer.size());
        const float readIndexFloat = static_cast<float>(inputWrite) - delaySamplesFloat;

        // Wrap and split into integer and frac
        float wrapped = readIndexFloat;

        while (wrapped < 0.0f)
            wrapped += static_cast<float>(size);

        int indexA = static_cast<int>(std::floor(wrapped)) % size;
        int indexB = (indexA - 1);
        while (indexB < 0) indexB += size;

        const float frac = wrapped - static_cast<float>(indexA);

        const float sampleA = inputBuffer[indexA];
        const float sampleB = inputBuffer[indexB];

        return sampleA * (1.0f - frac) + sampleB * frac;
    }

    float readOutputDelaySamplesFractional(float delaySamplesFloat) const
    {
        const int size = static_cast<int>(outputBuffer.size());
        const float readIndexFloat = static_cast<float>(outputWrite) - delaySamplesFloat;

        float wrapped = readIndexFloat;

        while (wrapped < 0.0f)
            wrapped += static_cast<float>(size);

        int indexA = static_cast<int>(std::floor(wrapped)) % size;
        int indexB = (indexA - 1);
        while (indexB < 0) indexB += size;

        const float frac = wrapped - static_cast<float>(indexA);

        const float sampleA = outputBuffer[indexA];
        const float sampleB = outputBuffer[indexB];

        return sampleA * (1.0f - frac) + sampleB * frac;
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