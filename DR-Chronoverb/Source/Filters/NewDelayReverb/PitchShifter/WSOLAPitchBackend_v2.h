#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

#include <juce_core/juce_core.h>

#include "PitchShiftingUtils.h"

// =====================================================================================
// WSOLAPitchBackend_v2
//
// Minimal WSOLA pitch backend for Chronoverb:
//
// - Input history ring for candidate search
// - Direct overlap-add into an output timeline ring
// - No separate stretch reservoir / no read-distance servo
// - Ratio changes only applied at segment scheduling boundaries
//
// Design intent:
// - Simpler and more debuggable than WSOLAPitchBackend v1
// - Better suited to Chronoverb's "pitch changes only at echo boundaries" rule
// - Focused on stable, recognizable output first
// =====================================================================================
class WSOLAPitchBackend_v2 : public IPitchShifterBackend
{
public:
    WSOLAPitchBackend_v2() = default;
    ~WSOLAPitchBackend_v2() override = default;

    void Prepare(double newSampleRate) override
    {
        sampleRate = newSampleRate;

        SetSegmentLengthMilliseconds(24.0f);
        SetOverlapPercent(0.75f);
        SetSearchRadiusMilliseconds(4.0f);
        SetLookbackMilliseconds(80.0f);

        rebuildParameters();
        rebuildWindow();

        // Input history ring
        inputRingSize = std::max(
            16384,
            lookbackSamples + segmentLengthSamples + (searchRadiusSamples * 2) + 4096
        );

        // Output OLA timeline ring
        outputRingSize = std::max(
            65536,
            segmentLengthSamples * 48
        );

        inputRing.assign(static_cast<size_t>(inputRingSize), 0.0f);
        outputRing.assign(static_cast<size_t>(outputRingSize), 0.0f);
        outputWeightRing.assign(static_cast<size_t>(outputRingSize), 0.0f);

        clearState();
    }

    void Reset() override
    {
        clearState();
    }

    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        if (inputRing.empty() || outputRing.empty() || outputWeightRing.empty())
            return inputSample;

        // 1) Write incoming sample into input history
        inputRing[static_cast<size_t>(inputWriteIndex)] = inputSample;
        inputWriteIndex = wrapInt(inputWriteIndex + 1, inputRingSize);

        if (startupSamplesReceived < std::numeric_limits<int>::max())
            ++startupSamplesReceived;

        // 2) Initialize once enough history exists
        if (!initialized)
            tryInitializeIfReady();

        // 3) Synthesize new segment when due
        if (initialized)
        {
            --samplesUntilNextSegment;

            if (samplesUntilNextSegment <= 0)
            {
                currentRatio = pendingRatio;

                analysisHopSamples = static_cast<float>(synthesisHopSamples);

                synthesizeNextSegment();

                samplesUntilNextSegment = std::max(1, static_cast<int>(std::round(
                    static_cast<float>(synthesisHopSamples) / currentRatio)));

                //samplesUntilNextSegment = synthesisHopSamples;
            }

            // 4) Read one sample from output timeline
            const float outputSample = readOutputRingLinearNormalized(outputReadIndex);

            outputReadIndex = wrapFloat(outputReadIndex + currentRatio, static_cast<float>(outputRingSize));

            // Optional conservative clear behind read head
            clearOldOutputData();

            if (!std::isfinite(outputSample))
                return 0.0f;

            return juce::jlimit(-2.0f, 2.0f, outputSample);
        }

        underflowCount.fetch_add(1, std::memory_order_relaxed);
        return 0.0f;
    }

    void OnEchoBoundary(float newRatio) override
    {
        pendingRatio = clampPitchRatio(newRatio);
    }

    void SetInitialRatio(float ratio) override
    {
        currentRatio = clampPitchRatio(ratio);
        pendingRatio = currentRatio;

        analysisHopSamples = static_cast<float>(synthesisHopSamples);
    }

    float GetLatencyMilliseconds() const override
    {
        return (static_cast<float>(lookbackSamples) * 1000.0f)
             / static_cast<float>(sampleRate);
    }

    // ---------------------------------------------------------------------
    // Parameter setters
    // ---------------------------------------------------------------------
    void SetSegmentLengthMilliseconds(float ms)
    {
        segmentLengthMs = std::clamp(ms, 8.0f, 60.0f);
        rebuildParameters();
        rebuildWindow();
    }

    void SetOverlapPercent(float percent)
    {
        overlapPercent = std::clamp(percent, 0.25f, 0.85f);
        rebuildParameters();
        rebuildWindow();
    }

    void SetSearchRadiusMilliseconds(float ms)
    {
        searchRadiusMs = std::clamp(ms, 0.5f, 12.0f);
        rebuildParameters();
    }

    void SetLookbackMilliseconds(float ms)
    {
        lookbackMs = std::clamp(ms, 20.0f, 200.0f);
        rebuildParameters();
    }

    // ---------------------------------------------------------------------
    // Debug accessors
    // ---------------------------------------------------------------------
    int GetUnderflowCount() const
    {
        return underflowCount.load(std::memory_order_relaxed);
    }

    float GetLastBestMatchScore() const
    {
        return lastBestMatchScore.load(std::memory_order_relaxed);
    }

    int GetLastBestMatchDelta() const
    {
        return lastBestMatchDelta.load(std::memory_order_relaxed);
    }

    float GetPredictedSourceIndexForDebug() const
    {
        return predictedSourceIndex;
    }

    float GetLastChosenSourceIndexForDebug() const
    {
        return lastChosenSourceIndex;
    }

    float GetAnalysisHopForDebug() const
    {
        return analysisHopSamples;
    }

    float GetStretchFactorForDebug() const
    {
        return currentRatio;
    }

    int GetSegmentLengthSamplesForDebug() const
    {
        return segmentLengthSamples;
    }

    int GetOverlapSamplesForDebug() const
    {
        return overlapSamples;
    }

    int GetSynthesisHopSamplesForDebug() const
    {
        return synthesisHopSamples;
    }

    int GetSamplesUntilNextSegmentForDebug() const
    {
        return samplesUntilNextSegment;
    }

private:
    // ---------------------------------------------------------------------
    // Core setup/state
    // ---------------------------------------------------------------------
    void rebuildParameters()
    {
        segmentLengthSamples = std::max(
            64,
            static_cast<int>(std::round((segmentLengthMs * sampleRate) / 1000.0))
        );

        overlapSamples = std::max(
            16,
            static_cast<int>(std::round(static_cast<float>(segmentLengthSamples) * overlapPercent))
        );

        overlapSamples = std::min(overlapSamples, segmentLengthSamples - 1);
        synthesisHopSamples = std::max(1, segmentLengthSamples - overlapSamples);

        searchRadiusSamples = std::max(
            8,
            static_cast<int>(std::round((searchRadiusMs * sampleRate) / 1000.0))
        );

        lookbackSamples = std::max(
            segmentLengthSamples * 2,
            static_cast<int>(std::round((lookbackMs * sampleRate) / 1000.0))
        );

        analysisHopSamples = static_cast<float>(synthesisHopSamples);
    }

    void rebuildWindow()
    {
        window.assign(static_cast<size_t>(segmentLengthSamples), 0.0f);

        if (segmentLengthSamples <= 1)
            return;

        for (int i = 0; i < segmentLengthSamples; ++i)
        {
            window[static_cast<size_t>(i)] =
                0.5f
                - 0.5f * std::cos(
                    2.0f * static_cast<float>(juce::MathConstants<float>::pi)
                    * static_cast<float>(i)
                    / static_cast<float>(segmentLengthSamples - 1));
        }
    }

    void clearState()
    {
        std::fill(inputRing.begin(), inputRing.end(), 0.0f);
        std::fill(outputRing.begin(), outputRing.end(), 0.0f);
        std::fill(outputWeightRing.begin(), outputWeightRing.end(), 0.0f);

        inputWriteIndex = 0;
        outputWriteCursor = 0;
        outputReadIndex = 0.0f;
        outputClearCursor = 0;

        startupSamplesReceived = 0;
        initialized = false;

        samplesUntilNextSegment = synthesisHopSamples;

        currentRatio = 1.0f;
        pendingRatio = 1.0f;

        analysisHopSamples = static_cast<float>(synthesisHopSamples);

        lastChosenSourceIndex = 0.0f;
        predictedSourceIndex = 0.0f;

        underflowCount.store(0, std::memory_order_relaxed);
        lastBestMatchScore.store(0.0f, std::memory_order_relaxed);
        lastBestMatchDelta.store(0, std::memory_order_relaxed);
    }

    void tryInitializeIfReady()
    {
        const int requiredHistory =
            lookbackSamples + segmentLengthSamples + searchRadiusSamples;

        if (startupSamplesReceived < requiredHistory)
            return;

        const int seedIndex =
            wrapInt(inputWriteIndex - lookbackSamples, inputRingSize);

        lastChosenSourceIndex = static_cast<float>(seedIndex);
        predictedSourceIndex = lastChosenSourceIndex;

        // Seed multiple segments so output timeline starts populated
        const int seedSegments = 8;
        for (int i = 0; i < seedSegments; ++i)
            synthesizeNextSegment();

        // Read behind the output write frontier, inside already written material
        outputReadIndex = wrapFloat(static_cast<float>(outputWriteCursor)
            - static_cast<float>(segmentLengthSamples * 2), static_cast<float>(outputRingSize));

        const int protectedDist = std::max(segmentLengthSamples * 6, 8192);

        outputClearCursor = wrapInt(static_cast<int>(outputReadIndex) - protectedDist,outputRingSize);

        samplesUntilNextSegment = synthesisHopSamples;
        initialized = true;
    }

    void synthesizeNextSegment()
    {
        const int predictedIndexInt =
            wrapInt(static_cast<int>(std::round(predictedSourceIndex)), inputRingSize);

        const int bestSourceIndex =
            findBestMatchingSourceIndex(predictedIndexInt);

        // Overlap-add selected input segment into output timeline
        for (int i = 0; i < segmentLengthSamples; ++i)
        {
            const float sourceSample =
                readInputRingLinear(static_cast<float>(bestSourceIndex + i));

            const float weight = window[static_cast<size_t>(i)];
            const int outputIndex =
                wrapInt(outputWriteCursor + i, outputRingSize);

            outputRing[static_cast<size_t>(outputIndex)] += sourceSample * weight;
            outputWeightRing[static_cast<size_t>(outputIndex)] += weight;
        }

        outputWriteCursor =
            wrapInt(outputWriteCursor + synthesisHopSamples, outputRingSize);

        lastChosenSourceIndex = static_cast<float>(bestSourceIndex);
        predictedSourceIndex =
            wrapFloat(lastChosenSourceIndex + analysisHopSamples,
                      static_cast<float>(inputRingSize));
    }

    int findBestMatchingSourceIndex(int predictedSourceIndexSamples) const
    {
        float bestScore = -std::numeric_limits<float>::max();
        int bestIndex = predictedSourceIndexSamples;
        bool foundValidCandidate = false;

        // Compare candidate head against tail overlap of the previously chosen segment
        const float referenceStart =
            lastChosenSourceIndex
            + static_cast<float>(segmentLengthSamples - overlapSamples);

        constexpr float distancePenaltyPerSample = 0.0001f;

        for (int delta = -searchRadiusSamples; delta <= searchRadiusSamples; ++delta)
        {
            const int candidateIndex =
                wrapInt(predictedSourceIndexSamples + delta, inputRingSize);

            if (!isCandidateCausallySafe(candidateIndex))
                continue;

            foundValidCandidate = true;

            float dot = 0.0f;
            float energyReference = 0.0f;
            float energyCandidate = 0.0f;

            for (int i = 0; i < overlapSamples; ++i)
            {
                const float referenceSample =
                    readInputRingLinear(referenceStart + static_cast<float>(i));

                const float candidateSample =
                    readInputRingLinear(static_cast<float>(candidateIndex + i));

                dot += referenceSample * candidateSample;
                energyReference += referenceSample * referenceSample;
                energyCandidate += candidateSample * candidateSample;
            }

            const float denominator =
                std::sqrt(std::max(energyReference * energyCandidate, 1.0e-12f));

            float score = dot / denominator;
            score -= distancePenaltyPerSample * std::abs(static_cast<float>(delta));

            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = candidateIndex;
                lastBestMatchDelta.store(delta, std::memory_order_relaxed);
            }
        }

        if (!foundValidCandidate)
        {
            lastBestMatchScore.store(0.0f, std::memory_order_relaxed);
            lastBestMatchDelta.store(0, std::memory_order_relaxed);
            return predictedSourceIndexSamples;
        }

        if (!std::isfinite(bestScore))
            bestScore = 0.0f;

        lastBestMatchScore.store(bestScore, std::memory_order_relaxed);
        return bestIndex;
    }

    float readInputRingLinear(float index) const
    {
        const float wrapped = wrapFloat(index, static_cast<float>(inputRingSize));

        const int indexA = static_cast<int>(std::floor(wrapped));
        const int indexB = wrapInt(indexA + 1, inputRingSize);
        const float frac = wrapped - static_cast<float>(indexA);

        const float a = inputRing[static_cast<size_t>(indexA)];
        const float b = inputRing[static_cast<size_t>(indexB)];

        return a + frac * (b - a);
    }

    float readOutputRingLinearNormalized(float index) const
    {
        const float wrapped = wrapFloat(index, static_cast<float>(outputRingSize));

        const int indexA = static_cast<int>(std::floor(wrapped));
        const int indexB = wrapInt(indexA + 1, outputRingSize);
        const float frac = wrapped - static_cast<float>(indexA);

        auto getNormalized = [&](int i) -> float
        {
            const float weight =
                std::max(outputWeightRing[static_cast<size_t>(i)], 1.0e-4f);

            return outputRing[static_cast<size_t>(i)] / weight;
        };

        const float a = getNormalized(indexA);
        const float b = getNormalized(indexB);

        return a + frac * (b - a);
    }

    void clearOldOutputData()
    {
        // Conservative clear: keep a large protected region around both read and write.
        const int readIndex =
            wrapInt(static_cast<int>(std::floor(outputReadIndex)), outputRingSize);

        const int protectedDistance =
            std::max(segmentLengthSamples * 6, 8192);

        const int stopIndex =
            wrapInt(readIndex - protectedDistance, outputRingSize);

        while (outputClearCursor != stopIndex)
        {
            outputRing[static_cast<size_t>(outputClearCursor)] = 0.0f;
            outputWeightRing[static_cast<size_t>(outputClearCursor)] = 0.0f;
            outputClearCursor = wrapInt(outputClearCursor + 1, outputRingSize);
        }
    }

    bool isCandidateCausallySafe(int candidateIndex) const
    {
        const int candidateEnd =
            wrapInt(candidateIndex + segmentLengthSamples - 1, inputRingSize);

        const int distanceToWrite =
            positiveDistanceForward(candidateEnd, inputWriteIndex, inputRingSize);

        constexpr int guardSamples = 8;
        return distanceToWrite >= guardSamples;
    }

    static float clampPitchRatio(float ratio)
    {
        return std::clamp(ratio, 0.25f, 4.0f);
    }

    static int wrapInt(int value, int size)
    {
        while (value < 0)
            value += size;

        while (value >= size)
            value -= size;

        return value;
    }

    static float wrapFloat(float value, float size)
    {
        while (value < 0.0f)
            value += size;

        while (value >= size)
            value -= size;

        return value;
    }

    static int positiveDistanceForward(int fromIndex, int toIndex, int size)
    {
        int distance = toIndex - fromIndex;

        while (distance < 0)
            distance += size;

        return distance;
    }

private:
    // Runtime configuration
    double sampleRate = 48000.0;

    // User-facing timing settings
    float segmentLengthMs = 24.0f;
    float overlapPercent = 0.75f;
    float searchRadiusMs = 4.0f;
    float lookbackMs = 80.0f;

    // Derived sample counts
    int segmentLengthSamples = 0;
    int overlapSamples = 0;
    int synthesisHopSamples = 0;
    int searchRadiusSamples = 0;
    int lookbackSamples = 0;

    // Input history
    std::vector<float> inputRing;
    int inputRingSize = 0;
    int inputWriteIndex = 0;

    // Output OLA timeline
    std::vector<float> outputRing;
    std::vector<float> outputWeightRing;
    int outputRingSize = 0;
    int outputWriteCursor = 0;
    float outputReadIndex = 0.0f;
    int outputClearCursor = 0;

    // Scheduling/window
    std::vector<float> window;
    int samplesUntilNextSegment = 0;

    // Source trajectory
    float currentRatio = 1.0f;
    float pendingRatio = 1.0f;
    float analysisHopSamples = 0.0f;

    float lastChosenSourceIndex = 0.0f;
    float predictedSourceIndex = 0.0f;

    int startupSamplesReceived = 0;
    bool initialized = false;

    // Debug
    mutable std::atomic<int> underflowCount { 0 };
    mutable std::atomic<float> lastBestMatchScore { 0.0f };
    mutable std::atomic<int> lastBestMatchDelta { 0 };
};