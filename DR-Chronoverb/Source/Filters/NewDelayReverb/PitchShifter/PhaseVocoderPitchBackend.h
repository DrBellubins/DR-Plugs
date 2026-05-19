#pragma once

#include <vector>
#include <deque>
#include <complex>
#include <cmath>
#include <algorithm>

#include "PitchShiftingUtils.h"

// =====================================================================================
// PhaseVocoderPitchBackend
//
// Single-channel phase-vocoder backend for Chronoverb's echo-quantized pitch shifts.
// The backend performs classic phase-vocoder time stretching, then resamples the
// stretched stream back to the original timebase so pitch is shifted without trying
// to retune individual FFT bins directly.
//
// Important behavioral characteristics:
// - Pitch ratio changes are quantized externally via OnEchoBoundary(newRatio).
// - The active ratio remains stable between boundaries.
// - ProcessSample() is sample-by-sample and outputs one sample per call.
// - Boundary changes crossfade between fully stateful time-stretch/resample paths.
// =====================================================================================
class PhaseVocoderPitchBackend : public IPitchShifterBackend
{
    struct PathState
    {
        std::vector<float> inputFifo;
        int inputFifoWriteIndex = 0;
        int inputWriteCount = 0;

        std::vector<float> frameInput;
        std::vector<std::complex<float>> fftBuffer;
        std::vector<std::complex<float>> ifftBuffer;

        std::vector<float> previousAnalysisPhase;
        std::vector<float> synthesisPhase;

        std::vector<float> stretchedOutputAccumulator;
        std::vector<float> stretchedOutputWindowSumAccumulator;
        int stretchedOutputReadIndex = 0;
        int stretchedOutputWriteBase = 0;

        // RT-safe ring buffer for stretched-domain samples (no allocations, no erases).
        std::vector<float> stretchedRing;
        int stretchedRingWrite = 0;
        int stretchedRingRead = 0;
        int stretchedRingCount = 0;

        float resampleReadPosition = 0.0f; // fractional index into the readable region
        float lastOutputSample = 0.0f;
    };

public:
    PhaseVocoderPitchBackend() = default;

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        juce::ignoreUnused(maximumBlockSize);

        sampleRate = newSampleRate;

        SetFFTSize(1024);
        SetAnalysisHop(128);
        SetBoundaryCrossfadeMilliseconds(10.0f);

        Reset();
    }

    void Reset() override
    {
        buildWindow();
        buildExpectedPhaseAdvance();

        resetPath(activePath);
        resetPath(pendingPath);

        currentRatio = 1.0f;
        pendingRatio = 1.0f;
        hasPendingRatio = false;
        crossfadeRemainingSamples = 0;
        crossfadeTotalSamples = boundaryCrossfadeSamples;
    }

    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        float out = processOnePath(inputSample, currentRatio, activePath);

        if (crossfadeRemainingSamples > 0)
        {
            const float pendingOut = processOnePath(inputSample, pendingRatio, pendingPath);

            const int samplesDone = crossfadeTotalSamples - crossfadeRemainingSamples;
            const float t =
                static_cast<float>(samplesDone)
                / static_cast<float>(std::max(1, crossfadeTotalSamples));

            const float oldGain = std::cos(t * juce::MathConstants<float>::halfPi);
            const float newGain = std::sin(t * juce::MathConstants<float>::halfPi);

            --crossfadeRemainingSamples;

            if (crossfadeRemainingSamples == 0)
                commitPendingState();

            out = out * oldGain + pendingOut * newGain;
        }

        if (!std::isfinite(out))
            out = 0.0f;

        out = juce::jlimit(-2.0f, 2.0f, out);

        if (std::abs(out) < 1.0e-20f)
            out = 0.0f;

        return out;
    }

    void OnEchoBoundary(float newRatio) override
    {
        const float clampedRatio = juce::jlimit(kMinPitchRatio, kMaxPitchRatio, newRatio);

        if (std::abs(clampedRatio - currentRatio) < 1.0e-6f)
            return;

        if (boundaryCrossfadeSamples <= 0)
        {
            currentRatio = clampedRatio;
            pendingRatio = clampedRatio;
            hasPendingRatio = false;
            crossfadeRemainingSamples = 0;
            return;
        }

        resetPath(pendingPath);
        pendingRatio = clampedRatio;
        hasPendingRatio = true;

        crossfadeTotalSamples = boundaryCrossfadeSamples;
        crossfadeRemainingSamples = crossfadeTotalSamples;
    }

    void SetInitialRatio(float ratio) override
    {
        currentRatio = juce::jlimit(kMinPitchRatio, kMaxPitchRatio, ratio);
        pendingRatio = currentRatio;
        hasPendingRatio = false;
        crossfadeRemainingSamples = 0;
    }

    void SetFFTSize(int newFFTSize)
    {
        fftSize = juce::jmax(256, nextPowerOfTwo(newFFTSize));
        numBins = (fftSize / 2) + 1;
    }

    void SetAnalysisHop(int newHopSize)
    {
        analysisHop = juce::jlimit(64, fftSize / 2, newHopSize);
    }

    void SetBoundaryCrossfadeMilliseconds(float ms)
    {
        const float clampedMs = juce::jlimit(0.0f, 30.0f, ms);
        boundaryCrossfadeSamples = std::max(
            0,
            static_cast<int>(std::round((clampedMs * sampleRate) / 1000.0)));
    }

    float GetLatencyMilliseconds() const override
    {
        return (static_cast<float>(fftSize) * 1000.0f) / static_cast<float>(sampleRate);
    }

private:
    static constexpr float kTwoPi = 6.28318530717958647692f;
    static constexpr float kMinPitchRatio = 0.25f;
    static constexpr float kMaxPitchRatio = 4.0f;
    static constexpr int kCubicInterpolationSampleCount = 4;
    static constexpr int kAccumulatorMultiplier = 8; // Leaves room for 4x stretch plus overlapping OLA frames.
    static constexpr int kResampleGuardSamples = kCubicInterpolationSampleCount * 2; // Leaves two cubic kernels of safety ahead of the read point between bursty frame writes.

    double sampleRate = 48000.0;

    int fftSize = 1024;
    int numBins = 513;
    int analysisHop = 128;
    int boundaryCrossfadeSamples = 0;

    float currentRatio = 1.0f;
    float pendingRatio = 1.0f;
    bool hasPendingRatio = false;

    int crossfadeTotalSamples = 0;
    int crossfadeRemainingSamples = 0;

    std::vector<float> window;
    std::vector<float> expectedPhaseAdvance;

    PathState activePath;
    PathState pendingPath;

    void buildWindow()
    {
        window.assign(static_cast<size_t>(fftSize), 0.0f);

        for (int i = 0; i < fftSize; ++i)
        {
            window[static_cast<size_t>(i)] =
                0.5f - 0.5f * std::cos(kTwoPi * static_cast<float>(i) / static_cast<float>(fftSize));
        }
    }

    void buildExpectedPhaseAdvance()
    {
        expectedPhaseAdvance.assign(static_cast<size_t>(numBins), 0.0f);

        for (int binIndex = 0; binIndex < numBins; ++binIndex)
        {
            expectedPhaseAdvance[static_cast<size_t>(binIndex)] =
                kTwoPi
                * static_cast<float>(analysisHop)
                * static_cast<float>(binIndex)
                / static_cast<float>(fftSize);
        }
    }

    void resetPath(PathState& path)
    {
        path.inputFifo.assign(static_cast<size_t>(fftSize), 0.0f);
        path.inputFifoWriteIndex = 0;
        path.inputWriteCount = 0;

        path.frameInput.assign(static_cast<size_t>(fftSize), 0.0f);
        path.fftBuffer.assign(static_cast<size_t>(fftSize), {});
        path.ifftBuffer.assign(static_cast<size_t>(fftSize), {});

        path.previousAnalysisPhase.assign(static_cast<size_t>(numBins), 0.0f);
        path.synthesisPhase.assign(static_cast<size_t>(numBins), 0.0f);

        const int accumulatorSize = fftSize * kAccumulatorMultiplier;
        path.stretchedOutputAccumulator.assign(static_cast<size_t>(accumulatorSize), 0.0f);
        path.stretchedOutputWindowSumAccumulator.assign(static_cast<size_t>(accumulatorSize), 0.0f);
        path.stretchedOutputReadIndex = 0;
        path.stretchedOutputWriteBase = fftSize;

        initRing(path);
    }

    void commitPendingState()
    {
        currentRatio = pendingRatio;
        hasPendingRatio = false;
        std::swap(activePath, pendingPath);
    }

    float processOnePath(float inputSample, float pitchRatio, PathState& path)
    {
        pushInputSample(path, inputSample);

        ++path.inputWriteCount;

        if (path.inputWriteCount >= analysisHop)
        {
            path.inputWriteCount = 0;

            const float clampedPitch = juce::jlimit(kMinPitchRatio, kMaxPitchRatio, pitchRatio);

            // Phase vocoder does time-stretch, then we resample back to the original timebase.
            // Pitch up (> 1.0) should time-stretch DOWN (< 1.0), pitch down (< 1.0) time-stretch UP (> 1.0).
            const float timeStretchRatio = 1.0f / clampedPitch;

            processFrame(path, timeStretchRatio);
        }

        // Resampler consumes stretched-domain samples using pitchRatio (not timeStretchRatio).
        return readResampledOutput(path, pitchRatio);
    }

    void pushInputSample(PathState& path, float sample)
    {
        path.inputFifo[static_cast<size_t>(path.inputFifoWriteIndex)] = sample;
        path.inputFifoWriteIndex = (path.inputFifoWriteIndex + 1) % static_cast<int>(path.inputFifo.size());
    }

    void processFrame(PathState& path, float timeStretchRatio)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            const int readIndex = (path.inputFifoWriteIndex + i) % fftSize;
            path.frameInput[static_cast<size_t>(i)] =
                path.inputFifo[static_cast<size_t>(readIndex)] * window[static_cast<size_t>(i)];
            path.fftBuffer[static_cast<size_t>(i)] =
                std::complex<float>(path.frameInput[static_cast<size_t>(i)], 0.0f);
        }

        fft(path.fftBuffer, false);
        std::fill(path.ifftBuffer.begin(), path.ifftBuffer.end(), std::complex<float>(0.0f, 0.0f));

        const int synthesisHop = getSynthesisHop(timeStretchRatio);

        for (int binIndex = 0; binIndex < numBins; ++binIndex)
        {
            const std::complex<float> binValue = path.fftBuffer[static_cast<size_t>(binIndex)];

            const float magnitude = std::abs(binValue);
            const float phase = std::atan2(binValue.imag(), binValue.real());

            float deltaPhase =
                phase
                - path.previousAnalysisPhase[static_cast<size_t>(binIndex)]
                - expectedPhaseAdvance[static_cast<size_t>(binIndex)];

            deltaPhase = wrapPhase(deltaPhase);

            const float trueFrequency =
                (expectedPhaseAdvance[static_cast<size_t>(binIndex)] + deltaPhase)
                / static_cast<float>(analysisHop);

            path.synthesisPhase[static_cast<size_t>(binIndex)] +=
                trueFrequency * static_cast<float>(synthesisHop);

            path.previousAnalysisPhase[static_cast<size_t>(binIndex)] = phase;

            const std::complex<float> rebuiltBin =
                std::polar(magnitude, path.synthesisPhase[static_cast<size_t>(binIndex)]);

            path.ifftBuffer[static_cast<size_t>(binIndex)] = rebuiltBin;

            if (binIndex > 0 && binIndex < fftSize / 2)
            {
                path.ifftBuffer[static_cast<size_t>(fftSize - binIndex)] =
                    std::conj(rebuiltBin);
            }
        }

        fft(path.ifftBuffer, true);
        overlapAddFrame(path);
        drainStretchedOutput(path, synthesisHop);

        const int accumulatorSize = static_cast<int>(path.stretchedOutputAccumulator.size());
        path.stretchedOutputWriteBase = (path.stretchedOutputWriteBase + synthesisHop) % accumulatorSize;
    }

    int getSynthesisHop(float timeStretchRatio) const
    {
        // timeStretchRatio range should be [1/maxPitch, 1/minPitch]
        const float clamped =
            juce::jlimit(1.0f / kMaxPitchRatio, 1.0f / kMinPitchRatio, timeStretchRatio);

        return std::max(1,
            static_cast<int>(std::round(static_cast<float>(analysisHop) * clamped)));
    }

    void overlapAddFrame(PathState& path)
    {
        const int accumulatorSize = static_cast<int>(path.stretchedOutputAccumulator.size());

        for (int i = 0; i < fftSize; ++i)
        {
            const float sample =
                (path.ifftBuffer[static_cast<size_t>(i)].real() / static_cast<float>(fftSize))
                * window[static_cast<size_t>(i)];

            const int accumulatorIndex = (path.stretchedOutputWriteBase + i) % accumulatorSize;

            path.stretchedOutputAccumulator[static_cast<size_t>(accumulatorIndex)] += sample;
            path.stretchedOutputWindowSumAccumulator[static_cast<size_t>(accumulatorIndex)] +=
                window[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        }
    }

    void drainStretchedOutput(PathState& path, int synthesisHop)
    {
        const int accumulatorSize = static_cast<int>(path.stretchedOutputAccumulator.size());

        for (int i = 0; i < synthesisHop; ++i)
        {
            const int readIndex = path.stretchedOutputReadIndex % accumulatorSize;
            const float windowSum = path.stretchedOutputWindowSumAccumulator[static_cast<size_t>(readIndex)];
            const float rawSample = path.stretchedOutputAccumulator[static_cast<size_t>(readIndex)];

            float normalizedSample = 0.0f;

            // Stronger guard: avoid dividing by tiny sums (spike generator).
            if (windowSum > 1.0e-3f)
                normalizedSample = rawSample / windowSum;

            if (!std::isfinite(normalizedSample))
                normalizedSample = 0.0f;

            normalizedSample = juce::jlimit(-2.0f, 2.0f, normalizedSample);

            path.stretchedOutputAccumulator[static_cast<size_t>(readIndex)] = 0.0f;
            path.stretchedOutputWindowSumAccumulator[static_cast<size_t>(readIndex)] = 0.0f;

            ringPush(path, normalizedSample);

            path.stretchedOutputReadIndex = (path.stretchedOutputReadIndex + 1) % accumulatorSize;
        }
    }

    float readResampledOutput(PathState& path, float pitchRatio)
    {
        // Need 4 samples for cubic.
        if (path.stretchedRingCount < kCubicInterpolationSampleCount)
            return path.lastOutputSample;

        const float clampedPitch = juce::jlimit(kMinPitchRatio, kMaxPitchRatio, pitchRatio);

        // Clamp read position to available region.
        const float maxReadable = static_cast<float>(path.stretchedRingCount - 1);
        const float readPos = juce::jlimit(0.0f, maxReadable, path.resampleReadPosition);

        const int index1 = static_cast<int>(std::floor(readPos));
        const float frac = readPos - static_cast<float>(index1);

        const float s0 = ringPeek(path, juce::jlimit(0, path.stretchedRingCount - 1, index1 - 1));
        const float s1 = ringPeek(path, juce::jlimit(0, path.stretchedRingCount - 1, index1));
        const float s2 = ringPeek(path, juce::jlimit(0, path.stretchedRingCount - 1, index1 + 1));
        const float s3 = ringPeek(path, juce::jlimit(0, path.stretchedRingCount - 1, index1 + 2));

        const float a0 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
        const float a1 = s0 - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
        const float a2 = -0.5f * s0 + 0.5f * s2;
        const float a3 = s1;

        float out = ((a0 * frac + a1) * frac + a2) * frac + a3;

        if (!std::isfinite(out))
            out = 0.0f;

        out = juce::jlimit(-2.0f, 2.0f, out);

        path.lastOutputSample = out;

        // Advance fractional read head.
        path.resampleReadPosition += clampedPitch;

        // Consume integer part, leaving guard samples in ring.
        const int guard = kResampleGuardSamples;
        const int availableToConsume = std::max(0, path.stretchedRingCount - guard);
        const int consume = std::min(static_cast<int>(std::floor(path.resampleReadPosition)), availableToConsume);

        if (consume > 0)
        {
            ringConsume(path, consume);
            path.resampleReadPosition -= static_cast<float>(consume);
        }

        return out;
    }

    static float cubicInterpolate(const std::deque<float>& samples, float position)
    {
        const int sampleCount = static_cast<int>(samples.size());
        const int index1 = static_cast<int>(std::floor(position));
        const float fraction = position - static_cast<float>(index1);

        const float sample0 = samples[static_cast<size_t>(clampIndex(index1 - 1, sampleCount))];
        const float sample1 = samples[static_cast<size_t>(clampIndex(index1, sampleCount))];
        const float sample2 = samples[static_cast<size_t>(clampIndex(index1 + 1, sampleCount))];
        const float sample3 = samples[static_cast<size_t>(clampIndex(index1 + 2, sampleCount))];

        const float a0 = -0.5f * sample0 + 1.5f * sample1 - 1.5f * sample2 + 0.5f * sample3;
        const float a1 = sample0 - 2.5f * sample1 + 2.0f * sample2 - 0.5f * sample3;
        const float a2 = -0.5f * sample0 + 0.5f * sample2;
        const float a3 = sample1;

        return ((a0 * fraction + a1) * fraction + a2) * fraction + a3;
    }

    static int clampIndex(int index, int size)
    {
        return juce::jlimit(0, std::max(0, size - 1), index);
    }

    static float wrapPhase(float phase)
    {
        while (phase > juce::MathConstants<float>::pi)
            phase -= kTwoPi;

        while (phase < -juce::MathConstants<float>::pi)
            phase += kTwoPi;

        return phase;
    }

    static int nextPowerOfTwo(int value)
    {
        int power = 1;

        while (power < value)
            power <<= 1;

        return power;
    }

    static void fft(std::vector<std::complex<float>>& data, bool inverse)
    {
        const int n = static_cast<int>(data.size());

        int j = 0;
        for (int i = 1; i < n; ++i)
        {
            int bit = n >> 1;

            while ((j & bit) != 0)
            {
                j ^= bit;
                bit >>= 1;
            }

            j ^= bit;

            if (i < j)
                std::swap(data[static_cast<size_t>(i)], data[static_cast<size_t>(j)]);
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const float angle =
                static_cast<float>((inverse ? 2.0 : -2.0) * juce::MathConstants<float>::pi / len);

            const std::complex<float> wLen(std::cos(angle), std::sin(angle));

            for (int i = 0; i < n; i += len)
            {
                std::complex<float> w(1.0f, 0.0f);

                for (int k = 0; k < len / 2; ++k)
                {
                    const std::complex<float> u = data[static_cast<size_t>(i + k)];
                    const std::complex<float> v = data[static_cast<size_t>(i + k + len / 2)] * w;

                    data[static_cast<size_t>(i + k)] = u + v;
                    data[static_cast<size_t>(i + k + len / 2)] = u - v;

                    w *= wLen;
                }
            }
        }

        // Normalize inverse FFT (required).
        if (inverse)
        {
            const float invN = 1.0f / static_cast<float>(n);
            for (int i = 0; i < n; ++i)
                data[static_cast<size_t>(i)] *= invN;
        }
    }

    // Ring buffer helpers
    static int wrapRingIndex(int i, int size)
    {
        while (i < 0) i += size;
        while (i >= size) i -= size;
        return i;
    }

    void initRing(PathState& path)
    {
        // Needs to comfortably absorb OLA bursts.
        // FFT 1024, accumulator multiplier 8 => plenty, but ring should be larger than one frame.
        const int minSize = fftSize * 16;
        path.stretchedRing.assign(static_cast<size_t>(minSize), 0.0f);
        path.stretchedRingWrite = 0;
        path.stretchedRingRead = 0;
        path.stretchedRingCount = 0;
        path.resampleReadPosition = 0.0f;
        path.lastOutputSample = 0.0f;
    }

    bool ringPush(PathState& path, float x)
    {
        const int size = static_cast<int>(path.stretchedRing.size());
        if (size <= 0) return false;

        // If full, drop oldest (keeps RT running; avoids overwrite bugs).
        if (path.stretchedRingCount >= size)
        {
            path.stretchedRingRead = wrapRingIndex(path.stretchedRingRead + 1, size);
            path.stretchedRingCount = size - 1;
            // Also shift fractional read position back if it pointed into dropped area.
            path.resampleReadPosition = juce::jmax(0.0f, path.resampleReadPosition - 1.0f);
        }

        path.stretchedRing[static_cast<size_t>(path.stretchedRingWrite)] = x;
        path.stretchedRingWrite = wrapRingIndex(path.stretchedRingWrite + 1, size);
        ++path.stretchedRingCount;
        return true;
    }

    float ringPeek(const PathState& path, int offsetFromRead) const
    {
        const int size = static_cast<int>(path.stretchedRing.size());
        const int idx = wrapRingIndex(path.stretchedRingRead + offsetFromRead, size);
        return path.stretchedRing[static_cast<size_t>(idx)];
    }

    void ringConsume(PathState& path, int n)
    {
        const int size = static_cast<int>(path.stretchedRing.size());
        const int consume = juce::jlimit(0, path.stretchedRingCount, n);

        path.stretchedRingRead = wrapRingIndex(path.stretchedRingRead + consume, size);
        path.stretchedRingCount -= consume;
    }
};
