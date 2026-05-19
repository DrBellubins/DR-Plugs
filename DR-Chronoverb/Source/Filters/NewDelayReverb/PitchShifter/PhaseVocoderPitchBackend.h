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

        std::deque<float> stretchedOutputFifo;
        float resampleReadPosition = 0.0f;
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
        const float currentOut = processOnePath(inputSample, currentRatio, activePath);

        if (crossfadeRemainingSamples > 0 && hasPendingRatio)
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

            return currentOut * oldGain + pendingOut * newGain;
        }

        return currentOut;
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

        pendingPath = activePath;
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
    static constexpr int kResampleGuardSamples = kCubicInterpolationSampleCount * 2; // Leaves one cubic kernel of safety ahead of the read point between bursty frame writes.

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

        path.stretchedOutputFifo.clear();
        path.resampleReadPosition = 0.0f;
        path.lastOutputSample = 0.0f;
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
            processFrame(path, pitchRatio);
        }

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
        const float clampedRatio = juce::jlimit(kMinPitchRatio, kMaxPitchRatio, timeStretchRatio);
        return std::max(1, static_cast<int>(std::round(static_cast<float>(analysisHop) * clampedRatio)));
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

            const float normalizedSample =
                (windowSum > 1.0e-6f) ? (rawSample / windowSum) : 0.0f;

            path.stretchedOutputAccumulator[static_cast<size_t>(readIndex)] = 0.0f;
            path.stretchedOutputWindowSumAccumulator[static_cast<size_t>(readIndex)] = 0.0f;

            path.stretchedOutputFifo.push_back(normalizedSample);
            path.stretchedOutputReadIndex = (path.stretchedOutputReadIndex + 1) % accumulatorSize;
        }
    }

    float readResampledOutput(PathState& path, float pitchRatio)
    {
        if (path.stretchedOutputFifo.size() < static_cast<size_t>(kCubicInterpolationSampleCount))
            return 0.0f;

        const float maxReadablePosition = static_cast<float>(path.stretchedOutputFifo.size() - 1);
        const float readPosition = juce::jlimit(0.0f, maxReadablePosition, path.resampleReadPosition);

        const float outputSample = cubicInterpolate(path.stretchedOutputFifo, readPosition);
        path.lastOutputSample = outputSample;

        path.resampleReadPosition += juce::jlimit(kMinPitchRatio, kMaxPitchRatio, pitchRatio);

        const int samplesAvailableToTrim =
            std::max(0, static_cast<int>(path.stretchedOutputFifo.size()) - kResampleGuardSamples);
        const int samplesToTrim = std::min(
            static_cast<int>(std::floor(path.resampleReadPosition)),
            samplesAvailableToTrim);

        if (samplesToTrim > 0)
        {
            path.stretchedOutputFifo.erase(
                path.stretchedOutputFifo.begin(),
                path.stretchedOutputFifo.begin() + samplesToTrim);
            path.resampleReadPosition -= static_cast<float>(samplesToTrim);
        }

        return outputSample;
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
    }
};
