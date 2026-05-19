#pragma once

#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

#include "PitchShiftingUtils.h"

// =====================================================================================
// PhaseVocoderPitchBackend
//
// Simple single-channel phase-vocoder pitch shifter for octave-per-echo use.
// Designed to fit Chronoverb's modular backend architecture.
//
// Important behavioral characteristics:
// - Pitch ratio changes are quantized externally via OnEchoBoundary(newRatio).
// - The active ratio remains stable between boundaries.
// - ProcessSample() is sample-by-sample and outputs one sample per call.
// - Internally performs block FFT analysis/resynthesis with overlap-add.
// - This is intentionally simple rather than ultra-optimized.
//
// Quality / latency balance:
// - Defaults to 2048 FFT, 256 analysis hop.
// - Good quality for wet echo/reverb path.
// - Latency roughly tied to frame size and overlap buffering.
//
// Notes:
// - The backend ignores the ProcessSample pitchRatio argument, just like the granular
//   backend ignores it, because Chronoverb's pitch should not jump mid-echo.
// - Ratio changes are applied only at echo boundaries using a short output crossfade.
// =====================================================================================
class PhaseVocoderPitchBackend : public IPitchShifterBackend
{
public:
    PhaseVocoderPitchBackend() = default;

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        juce::ignoreUnused(maximumBlockSize);

        sampleRate = newSampleRate;

        SetFFTSize(2048);
        SetAnalysisHop(256);
        SetBoundaryCrossfadeMilliseconds(8.0f);

        Reset();
    }

    void Reset() override
    {
        inputFifo.assign(static_cast<size_t>(fftSize), 0.0f);
        inputWriteCount = 0;

        const int accSize = fftSize * 4;

        outputAccumulator.assign(static_cast<size_t>(accSize), 0.0f);
        outputWindowSumAccumulator.assign(static_cast<size_t>(accSize), 0.0f); // NEW

        // FIX: write pointer starts fftSize ahead of read pointer so the
        // overlap-add region is fully populated before we drain it (Fix 4).
        outputReadIndex  = 0;
        outputWriteBase  = fftSize;

        pendingOutputAccumulator.assign(static_cast<size_t>(accSize), 0.0f);
        pendingOutputWindowSumAccumulator.assign(static_cast<size_t>(accSize), 0.0f); // NEW

        pendingOutputReadIndex  = 0;
        pendingOutputWriteBase  = fftSize;

        // --- rest of Reset() unchanged below ---
        frameInput.assign(static_cast<size_t>(fftSize), 0.0f);
        fftBuffer.assign(static_cast<size_t>(fftSize), {});
        ifftBuffer.assign(static_cast<size_t>(fftSize), {});

        previousAnalysisPhase.assign(static_cast<size_t>(numBins), 0.0f);
        synthesisPhase.assign(static_cast<size_t>(numBins), 0.0f);

        pendingPreviousAnalysisPhase.assign(static_cast<size_t>(numBins), 0.0f);
        pendingSynthesisPhase.assign(static_cast<size_t>(numBins), 0.0f);

        inputFifoWriteIndex = 0;
        pendingInputFifoWriteIndex = 0;

        outputFifo.clear();

        pendingOutputFifo.clear();

        currentRatio = 1.0f;
        pendingRatio = 1.0f;
        hasPendingRatio = false;

        crossfadeRemainingSamples = 0;
        crossfadeTotalSamples = boundaryCrossfadeSamples;

        pendingInputFifo.assign(static_cast<size_t>(fftSize), 0.0f);
        pendingInputWriteCount = 0;

        buildWindow();
        buildExpectedPhaseAdvance();
    }

    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        const float currentOut = processOnePath(
            inputSample,
            currentRatio,
            inputFifo,
            inputFifoWriteIndex,
            inputWriteCount,
            previousAnalysisPhase,
            synthesisPhase,
            outputAccumulator,
            outputWindowSumAccumulator,
            outputReadIndex,
            outputWriteBase,
            outputFifo);

        if (crossfadeRemainingSamples > 0)
        {
            const float pendingOut = processOnePath(
                inputSample,
                pendingRatio,
                pendingInputFifo,
                pendingInputFifoWriteIndex,
                pendingInputWriteCount,
                pendingPreviousAnalysisPhase,
                pendingSynthesisPhase,
                pendingOutputAccumulator,
                pendingOutputWindowSumAccumulator,
                pendingOutputReadIndex,
                pendingOutputWriteBase,
                pendingOutputFifo);

            const int samplesDone =
                crossfadeTotalSamples - crossfadeRemainingSamples;

            const float t =
                static_cast<float>(samplesDone)
                / static_cast<float>(std::max(1, crossfadeTotalSamples));

            const float oldGain = std::cos(t * juce::MathConstants<float>::halfPi);
            const float newGain = std::sin(t * juce::MathConstants<float>::halfPi);

            --crossfadeRemainingSamples;

            if (crossfadeRemainingSamples == 0)
            {
                commitPendingState();
            }

            return currentOut * oldGain + pendingOut * newGain;
        }

        return currentOut;
    }

    void OnEchoBoundary(float newRatio)
    {
        const float clampedRatio = juce::jlimit(0.25f, 4.0f, newRatio);

        if (std::abs(clampedRatio - currentRatio) < 1.0e-6f)
        {
            return;
        }

        startPendingPathFromCurrentState(clampedRatio);

        crossfadeTotalSamples = boundaryCrossfadeSamples;
        crossfadeRemainingSamples = crossfadeTotalSamples;
        hasPendingRatio = true;
    }

    void SetInitialRatio(float ratio)
    {
        currentRatio = juce::jlimit(0.25f, 4.0f, ratio);
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

    float GetLatencyMilliseconds() const
    {
        return (static_cast<float>(fftSize) * 1000.0f) / static_cast<float>(sampleRate);
    }

private:
    static constexpr float kTwoPi = 6.28318530717958647692f;

    double sampleRate = 48000.0;

    int fftSize = 2048;
    int numBins = 1025;
    int analysisHop = 256;
    int boundaryCrossfadeSamples = 0;

    float currentRatio = 1.0f;
    float pendingRatio = 1.0f;
    bool hasPendingRatio = false;

    int crossfadeTotalSamples = 0;
    int crossfadeRemainingSamples = 0;

    std::vector<float> window;
    std::vector<float> expectedPhaseAdvance;

    // Active path
    std::vector<float> inputFifo;
    int inputWriteCount = 0;

    std::vector<float> outputAccumulator;
    int outputReadIndex = 0;
    int outputWriteBase = 0;

    std::vector<float> frameInput;
    std::vector<std::complex<float>> fftBuffer;
    std::vector<std::complex<float>> ifftBuffer;

    std::vector<float> previousAnalysisPhase;
    std::vector<float> synthesisPhase;

    std::deque<float> outputFifo;
    std::deque<float> pendingOutputFifo;

    // Pending path for boundary crossfade
    std::vector<float> pendingInputFifo;
    int pendingInputWriteCount = 0;

    std::vector<float> pendingOutputAccumulator;
    int pendingOutputReadIndex = 0;
    int pendingOutputWriteBase = 0;

    int inputFifoWriteIndex = 0;
    int pendingInputFifoWriteIndex = 0;

    std::vector<float> pendingPreviousAnalysisPhase;
    std::vector<float> pendingSynthesisPhase;

    // Active path OLA window-sum accumulators
    std::vector<float> outputWindowSumAccumulator;

    // Pending path OLA window-sum accumulators
    std::vector<float> pendingOutputWindowSumAccumulator;

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

    void startPendingPathFromCurrentState(float newRatio)
    {
        pendingRatio = newRatio;

        pendingInputFifo = inputFifo;
        pendingInputWriteCount = inputWriteCount;
        pendingInputFifoWriteIndex = inputFifoWriteIndex;

        pendingOutputAccumulator = outputAccumulator;
        pendingOutputWindowSumAccumulator = outputWindowSumAccumulator;
        pendingOutputReadIndex = outputReadIndex;
        pendingOutputWriteBase = outputWriteBase;

        pendingPreviousAnalysisPhase = previousAnalysisPhase;
        pendingSynthesisPhase = synthesisPhase;

        pendingOutputFifo = outputFifo;
    }

    void commitPendingState()
    {
        currentRatio = pendingRatio;
        hasPendingRatio = false;

        inputFifo.swap(pendingInputFifo);
        inputWriteCount = pendingInputWriteCount;
        std::swap(inputFifoWriteIndex, pendingInputFifoWriteIndex);

        outputAccumulator.swap(pendingOutputAccumulator);
        outputWindowSumAccumulator.swap(pendingOutputWindowSumAccumulator);

        outputReadIndex = pendingOutputReadIndex;
        outputWriteBase = pendingOutputWriteBase;

        previousAnalysisPhase.swap(pendingPreviousAnalysisPhase);
        synthesisPhase.swap(pendingSynthesisPhase);

        outputFifo.swap(pendingOutputFifo);
    }

    float processOnePath(
        float inputSample,
        float pitchRatio,
        std::vector<float>& pathInputFifo,
        int& pathInputFifoWriteIdx,
        int& pathInputWriteCount,
        std::vector<float>& pathPreviousAnalysisPhase,
        std::vector<float>& pathSynthesisPhase,
        std::vector<float>& pathOutputAccumulator,
        std::vector<float>& pathWindowSumAccumulator,
        int& pathOutputReadIndex,
        int& pathOutputWriteBase,
        std::deque<float>& pathOutputFifo)
    {
        pushInputSample(pathInputFifo, pathInputFifoWriteIdx, inputSample);

        ++pathInputWriteCount;

        if (pathInputWriteCount >= analysisHop)
        {
            pathInputWriteCount = 0;
            processFrame(
                pitchRatio,
                pathInputFifo,
                pathInputFifoWriteIdx,
                pathPreviousAnalysisPhase,
                pathSynthesisPhase,
                pathOutputAccumulator,
                pathWindowSumAccumulator,
                pathOutputReadIndex,
                pathOutputWriteBase,
                pathOutputFifo);
        }

        float outputSample = 0.0f;

        if (!pathOutputFifo.empty())
        {
            outputSample = pathOutputFifo.front();
            pathOutputFifo.pop_front();   // O(1) on deque
        }

        return outputSample;
    }

    // O(1) circular write — no more std::rotate
    void pushInputSample(std::vector<float>& fifo, int& writeIdx, float sample)
    {
        fifo[static_cast<size_t>(writeIdx)] = sample;
        writeIdx = (writeIdx + 1) % static_cast<int>(fifo.size());
    }

    void processFrame(
        float pitchRatio,
        std::vector<float>& pathInputFifo,
        int pathInputFifoWriteIdx,
        std::vector<float>& pathPreviousAnalysisPhase,
        std::vector<float>& pathSynthesisPhase,
        std::vector<float>& pathOutputAccumulator,
        std::vector<float>& pathWindowSumAccumulator,
        int& pathOutputReadIndex,
        int& pathOutputWriteBase,
        std::deque<float>& pathOutputFifo)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            // pathInputFifoWriteIdx points one past the newest sample = oldest slot
            const int readIdx = (pathInputFifoWriteIdx + i) % fftSize;

            frameInput[static_cast<size_t>(i)] =
                pathInputFifo[static_cast<size_t>(readIdx)] * window[static_cast<size_t>(i)];
            fftBuffer[static_cast<size_t>(i)] =
                std::complex<float>(frameInput[static_cast<size_t>(i)], 0.0f);
        }

        fft(fftBuffer, false);

        std::fill(ifftBuffer.begin(), ifftBuffer.end(), std::complex<float>(0.0f, 0.0f));

        for (int binIndex = 0; binIndex < numBins; ++binIndex)
        {
            const std::complex<float> binValue = fftBuffer[static_cast<size_t>(binIndex)];

            const float magnitude = std::abs(binValue);
            const float phase = std::atan2(binValue.imag(), binValue.real());

            float deltaPhase =
                phase
                - pathPreviousAnalysisPhase[static_cast<size_t>(binIndex)]
                - expectedPhaseAdvance[static_cast<size_t>(binIndex)];

            deltaPhase = wrapPhase(deltaPhase);

            const float trueFrequency =
                (expectedPhaseAdvance[static_cast<size_t>(binIndex)] + deltaPhase)
                / static_cast<float>(analysisHop);

            pathSynthesisPhase[static_cast<size_t>(binIndex)] +=
                trueFrequency * static_cast<float>(analysisHop) * pitchRatio;

            pathPreviousAnalysisPhase[static_cast<size_t>(binIndex)] = phase;

            const std::complex<float> rebuiltBin =
                std::polar(magnitude, pathSynthesisPhase[static_cast<size_t>(binIndex)]);

            ifftBuffer[static_cast<size_t>(binIndex)] = rebuiltBin;

            if (binIndex > 0 && binIndex < fftSize / 2)
            {
                ifftBuffer[static_cast<size_t>(fftSize - binIndex)] =
                    std::conj(rebuiltBin);
            }
        }

        fft(ifftBuffer, true);

        const int accSize = static_cast<int>(pathOutputAccumulator.size());

        for (int i = 0; i < fftSize; ++i)
        {
            const float sample =
                (ifftBuffer[static_cast<size_t>(i)].real() / static_cast<float>(fftSize))
                * window[static_cast<size_t>(i)];

            const int accIdx = (pathOutputWriteBase + i) % accSize;

            pathOutputAccumulator[static_cast<size_t>(accIdx)] += sample;

            pathWindowSumAccumulator[static_cast<size_t>(accIdx)] +=
                window[static_cast<size_t>(i)] * window[static_cast<size_t>(i)];
        }

        for (int i = 0; i < analysisHop; ++i)
        {
            const int readIdx = pathOutputReadIndex % accSize;

            const float winSum = pathWindowSumAccumulator[static_cast<size_t>(readIdx)];
            const float raw = pathOutputAccumulator[static_cast<size_t>(readIdx)];

            // Fix 3: divide by accumulated window² to normalise OLA amplitude.
            const float normalized = (winSum > 1.0e-6f) ? (raw / winSum) : 0.0f;

            pathOutputAccumulator[static_cast<size_t>(readIdx)]    = 0.0f;
            pathWindowSumAccumulator[static_cast<size_t>(readIdx)] = 0.0f;

            pathOutputFifo.push_back(normalized);
            pathOutputReadIndex = (pathOutputReadIndex + 1) % accSize;
        }

        pathOutputWriteBase = (pathOutputWriteBase + analysisHop)
                      % static_cast<int>(pathOutputAccumulator.size());
    }

    int pathOutputReadIndexPlaceholder(int pathOutputWriteBase, int sampleOffset) const
    {
        return pathOutputWriteBase + sampleOffset;
    }

    void wrapAccumulatorIndices(
        std::vector<float>& accumulator,
        int& readIndex,
        int& writeBase)
    {
        const int size = static_cast<int>(accumulator.size());

        if (size <= 0)
            return;

        while (readIndex >= size)
            readIndex -= size;

        while (writeBase >= size)
            writeBase -= size;
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

    // -----------------------------------------------------------------------------
    // Simple radix-2 Cooley-Tukey FFT
    // -----------------------------------------------------------------------------
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