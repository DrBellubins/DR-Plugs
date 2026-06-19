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
class DeverbDiffusionAllpass
{
public:
    DeverbDiffusionAllpass() = default;

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
        currentDelaySamples = static_cast<float>(delaySamplesInteger);
        Clear();
    }

    float ProcessSample(float inputSample)
    {
        const int size = static_cast<int>(buffer.size());

        // Use target directly — LFO is already smooth, no per-sample slewing needed
        float readSamples = (targetDelayMs * static_cast<float>(sampleRate)) / 1000.0f;
        float readPos = static_cast<float>(writeIndex) - readSamples;

        while (readPos < 0.0f)
            readPos += static_cast<float>(size);

        while (readPos >= static_cast<float>(size))
            readPos -= static_cast<float>(size);

        const int indexA = static_cast<int>(readPos);
        const int indexB = (indexA + 1) % size;
        const float frac = readPos - static_cast<float>(indexA);

        const float delayed = buffer[indexA] * (1.0f - frac) + buffer[indexB] * frac;

        const float v = inputSample + gain * delayed;
        const float y = delayed - gain * v;

        buffer[writeIndex] = v;

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
        currentDelaySamples = static_cast<float>(delaySamplesInteger);
    }

    // Compatibility path for existing DiffusionChain modulation calls.
    // For this cheap test version, we quantize to integer samples.
    void SetCurrentDelaySamples(float newDelaySamples)
    {
        currentDelaySamples = juce::jlimit(1.0f,
                                           static_cast<float>(maxUsableDelaySamples()),
                                           newDelaySamples);
    }

    void SetDelayMilliseconds(float newDelayMs)
    {
        delayMs = std::max(1.0f, newDelayMs);

        delaySamplesInteger = std::max(
            1,
            static_cast<int>(std::round((delayMs * static_cast<float>(sampleRate)) / 1000.0f)));

        ensureBufferSize();

        currentDelaySamples = static_cast<float>(std::min(delaySamplesInteger, maxUsableDelaySamples()));

        // Initialize target to prevent startup discontinuity
        targetDelayMs = delayMs;
    }

    void SetJitterSmoothingAlpha(float newAlpha)
    {
        jitterSmoothingAlpha = juce::jlimit(0.0f, 1.0f, newAlpha);
    }

    void SetMaxJitterDepthMs(float maxDepthMs)
    {
        maxJitterDepthMs = std::max(0.0f, maxDepthMs);
        ensureBufferSize(); // Re-evaluate buffer size
    }

    void SetTargetDelayMilliseconds(float newDelayMs)
    {
        targetDelayMs = std::max(1.0f, newDelayMs);
    }

    void Clear()
    {
        std::ranges::fill(buffer, 0.0f);
        writeIndex = 0;
    }

private:
    void ensureBufferSize()
    {
        // Extra headroom: +4 for interpolation safety, and enough
        // for the maximum jitter offset that could be applied later.
        const int maxJitterSamples = static_cast<int>(
            std::ceil((maxJitterDepthMs * static_cast<float>(sampleRate)) / 1000.0f));

        const int minSize = std::max(4, delaySamplesInteger + maxJitterSamples + 4);

        if (static_cast<int>(buffer.size()) < minSize)
            buffer.resize(static_cast<size_t>(minSize), 0.0f);
    }

    [[nodiscard]] int maxUsableDelaySamples() const
    {
        return std::max(1, static_cast<int>(buffer.size()) - 1);
    }

    double sampleRate = 48000.0;
    float delayMs = 50.0f;
    float gain = 0.65f;

    float targetDelayMs = 50.0f;
    float jitterSmoothingAlpha = 0.0f;
    float maxJitterDepthMs = 0.35f;

    std::vector<float> buffer;
    int writeIndex = 0;

    int delaySamplesInteger = 1;

    float currentDelaySamples = 1.0f;
};