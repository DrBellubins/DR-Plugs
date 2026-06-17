#pragma once

#include <vector>
#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

// DiffusionAllpass
// Cheap single-buffer Schroeder-style delay-line allpass:
//
//   d = delayed sample
//   v = x + g * d
//   y = d - g * v
//   buffer[write] = v
//
// This is much cheaper than the previous dual-buffer fractional-read version.
// It is intended as a test replacement to evaluate whether the old allpass
// topology was the main CPU bottleneck.
//
// Notes:
// - Uses one circular buffer
// - One delayed read
// - One write
// - Two multiplies + a few adds per sample
// - Supports integer delay directly
// - SetCurrentDelaySamples() is kept for compatibility with existing callers,
//   but this test implementation rounds to the nearest integer sample delay.
class DiffusionAllpass
{
public:
    DiffusionAllpass() = default;

    void Prepare(double newSampleRate)
    {
        sampleRate = std::max(1.0, newSampleRate);
        SetDelayMilliseconds(50.0f);
        SetGain(0.65f);
        ensureBufferSize();
        Clear();
    }

    void Configure(float delayMilliseconds, float newGain)
    {
        SetDelayMilliseconds(delayMilliseconds);
        SetGain(newGain);
        ensureBufferSize();
        currentDelaySamples = delaySamplesInteger;
        Clear();
    }

    float ProcessSample(float inputSample)
    {
        const int size = static_cast<int>(buffer.size());

        int readIndex = writeIndex - currentDelaySamples;

        while (readIndex < 0)
            readIndex += size;

        while (readIndex >= size)
            readIndex -= size;

        const float delayed = buffer[static_cast<size_t>(readIndex)];

        // Canonical Schroeder allpass
        const float v = inputSample + gain * delayed;
        const float y = delayed - gain * v;

        buffer[static_cast<size_t>(writeIndex)] = v;

        ++writeIndex;
        if (writeIndex >= size)
            writeIndex = 0;

        return y;
    }

    void SetGain(float newGain)
    {
        gain = juce::jlimit(-0.99f, 0.99f, newGain);
    }

    void SetBaseDelayMilliseconds(float newDelayMs)
    {
        SetDelayMilliseconds(newDelayMs);
        currentDelaySamples = delaySamplesInteger;
    }

    // Compatibility path for existing DiffusionChain modulation calls.
    // For this cheap test version, we quantize to integer samples.
    void SetCurrentDelaySamples(float newDelaySamples)
    {
        const int newDelayInt = std::max(1, static_cast<int>(std::round(newDelaySamples)));
        currentDelaySamples = std::min(newDelayInt, maxUsableDelaySamples());
    }

    void SetDelayMilliseconds(float newDelayMs)
    {
        delayMs = std::max(1.0f, newDelayMs);

        delaySamplesInteger = std::max(
            1,
            static_cast<int>(std::round((delayMs * static_cast<float>(sampleRate)) / 1000.0f)));

        ensureBufferSize();

        currentDelaySamples = std::min(delaySamplesInteger, maxUsableDelaySamples());
    }

    void Clear()
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

private:
    void ensureBufferSize()
    {
        const int minSize = std::max(4, delaySamplesInteger + 2);

        if (static_cast<int>(buffer.size()) < minSize)
            buffer.resize(static_cast<size_t>(minSize), 0.0f);
    }

    int maxUsableDelaySamples() const
    {
        return std::max(1, static_cast<int>(buffer.size()) - 1);
    }

    double sampleRate = 48000.0;
    float delayMs = 50.0f;
    float gain = 0.65f;

    std::vector<float> buffer;
    int writeIndex = 0;

    int delaySamplesInteger = 1;
    int currentDelaySamples = 1;
};