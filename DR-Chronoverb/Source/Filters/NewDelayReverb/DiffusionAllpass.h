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
        SetGain(0.65f);              // default
        Clear();
    }

    void Configure(float delayMilliseconds, float gain)
    {
        SetDelayMilliseconds(delayMilliseconds);
        SetGain(gain);
        ensureBufferSize();

        // Snap fractional delay state immediately so the slew limiter in
        // SetCurrentDelaySamples does not ramp from the 2400-sample default.
        delaySamplesInteger  = delaySamples;
        currentDelaySamples  = static_cast<float>(delaySamples);

        Clear();
    }

    float ProcessSample(float inputSample)
    {
        // Add denormal killing at input
        const float inputKilled = std::abs(inputSample) < 1e-15f ? 0.0f : inputSample;

        const float xDelayed = readDelaySamplesFractional(currentDelaySamples);
        const float yDelayed = readOutputDelaySamplesFractional(currentDelaySamples);

        // Kill denormals in delayed reads
        const float xD = std::abs(xDelayed) < 1e-15f ? 0.0f : xDelayed;
        const float yD = std::abs(yDelayed) < 1e-15f ? 0.0f : yDelayed;

        const float y = -g * inputKilled + xD + g * yD;

        // Kill denormals in output before storing
        const float yKilled = std::abs(y) < 1e-15f ? 0.0f : y;

        pushInput(inputKilled);
        pushOutput(yKilled);

        return yKilled;
    }

    void SetGain(float newGain)
    {
        g = std::max(-0.99f, std::min(0.99f, newGain));
    }

    // Set a base delay in milliseconds once (Configure calls this).
    void SetBaseDelayMilliseconds(float newDelayMs)
    {
        delayMs              = std::max(1.0f, newDelayMs);
        delaySamplesInteger  = static_cast<int>(std::floor((delayMs * sr) / 1000.0));
        ensureBufferSize();

        // Always snap both; callers that want a gentle slew should call
        // SetCurrentDelaySamples afterwards.
        delaySamples        = delaySamplesInteger;
        currentDelaySamples = static_cast<float>(delaySamplesInteger);
    }

    // Update the current fractional delay in samples without reallocating/clearing buffers.
    // Use this per-sample for jitter modulation.
    void SetCurrentDelaySamples(float newDelaySamples)
    {
        // Increase slew rate for jitter stability
        const float maxDelta = 0.5f; // Was 0.05f
        const float delta = juce::jlimit(-maxDelta, maxDelta, newDelaySamples - currentDelaySamples);
        currentDelaySamples += delta;

        const float minD = 2.0f; // Was 1.0f - avoid fractional blow-up near 0
        const float maxD = std::max(8.0f, static_cast<float>(inputBuffer.size()) - 3.0f);
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
        float wrapped = static_cast<float>(inputWrite) - delaySamplesFloat;

        // Wrap to [0, size)
        while (wrapped < 0.0f)
            wrapped += static_cast<float>(size);
        wrapped = std::fmod(wrapped, static_cast<float>(size)); // Add this!

        const float floorWrapped = std::floor(wrapped);
        const float frac = wrapped - floorWrapped;

        // Add safety clamp
        const float fracClamped = juce::jlimit(0.0f, 1.0f, frac);

        int indexA = static_cast<int>(floorWrapped) % size;
        int indexB = (indexA + 1) % size; // Changed from indexA - 1

        return inputBuffer[indexA] * (1.0f - fracClamped) + inputBuffer[indexB] * fracClamped;
    }

    float readOutputDelaySamplesFractional(float delaySamplesFloat) const
    {
        const int size = static_cast<int>(outputBuffer.size());
        const float readIndexFloat = static_cast<float>(outputWrite) - delaySamplesFloat;

        float wrapped = readIndexFloat;

        while (wrapped < 0.0f)
            wrapped += static_cast<float>(size);

        // Also clamp upper bound — previously missing, causes frac blow-up when wrapped >= size
        while (wrapped >= static_cast<float>(size))
            wrapped -= static_cast<float>(size);

        const float floorWrapped = std::floor(wrapped);
        const float frac = wrapped - floorWrapped;  // always in [0, 1)

        int indexA = static_cast<int>(floorWrapped) % size;
        int indexB = (indexA - 1 + size) % size;

        return outputBuffer[indexA] * (1.0f - frac) + outputBuffer[indexB] * frac;
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