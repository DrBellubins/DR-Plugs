#pragma once

#include <vector>
#include <atomic>
#include <limits>
#include <cmath>
#include <algorithm>

#include <juce_core/juce_core.h>

#include "PitchShiftingUtils.h"

// =====================================================================================
// WSOLAPitchBackend
//
// Refactored realtime WSOLA-style pitch shifter for isolated backend testing.
//
// Key design changes vs earlier prototype:
// - Stable intermediate "stretch" reservoir.
// - Explicit read-distance control with smoothed increment.
// - Proper delayed reclamation of old stretch data.
// - Cubic readout from normalized stretch ring.
// - Fixed-rate WSOLA synthesis into stretch domain.
// - Ratio changes only at echo boundaries.
//
// Notes:
// - This is still a minimal practical backend, not a commercial timestretcher.
// - Keep this isolated outside Chronoverb's feedback loop until it is acceptable.
// =====================================================================================
class WSOLAPitchBackend : public IPitchShifterBackend
{
public:
    WSOLAPitchBackend() = default;
    ~WSOLAPitchBackend() override = default;

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        sampleRate = newSampleRate;
        maximumBlockSizeCached = maximumBlockSize;

        SetSegmentLengthMilliseconds(24.0f);
        SetOverlapPercent(0.75f);
        SetSearchRadiusMilliseconds(4.0f);
        SetLookbackMilliseconds(70.0f);

        rebuildParametersFromTimeSettings();

        // Input history ring
        inputRingSize = std::max(
            8192,
            lookbackSamples + segmentLengthSamples + (searchRadiusSamples * 2) + 2048
        );

        // Stretch ring: intentionally generous so we can maintain a stable reservoir.
        stretchRingSize = std::max(
            32768,
            segmentLengthSamples * 24
        );

        inputRing.assign(static_cast<size_t>(inputRingSize), 0.0f);
        stretchRing.assign(static_cast<size_t>(stretchRingSize), 0.0f);
        stretchWeightRing.assign(static_cast<size_t>(stretchRingSize), 0.0f);

        rebuildWindow();
        clearBuffersAndState();
    }

    void Reset() override
    {
        clearBuffersAndState();
    }

    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        if (inputRing.empty() || stretchRing.empty() || stretchWeightRing.empty())
            return inputSample;

        // 1) Write input history
        inputRing[static_cast<size_t>(inputWriteIndex)] = inputSample;
        inputWriteIndex = wrapInt(inputWriteIndex + 1, inputRingSize);

        if (startupSamplesReceived < std::numeric_limits<int>::max())
            ++startupSamplesReceived;

        // 2) Initialize once enough history exists
        if (!initialized)
            tryInitializeIfReady();

        // 3) Synthesize WSOLA segments at fixed synthesis cadence
        if (initialized)
        {
            --samplesUntilNextSegment;

            if (samplesUntilNextSegment <= 0)
            {
                synthesizeNextSegment();

                samplesUntilNextSegment = static_cast<int>(
                    std::round(static_cast<float>(synthesisHopSamples) / stretchFactor));
            }
        }

        // Read from the anchored position
        const float ringSizeF = static_cast<float>(stretchRingSize);

        float outputSample = 0.0f;

        if (initialized)
        {
            outputSample = readStretchRingNormalizedCubic(stretchReadIndexFloat);

            stretchReadIndexFloat =
                wrapFloat(stretchReadIndexFloat + stretchFactor, ringSizeF);

            const float desiredReadIndex =
                wrapFloat(static_cast<float>(stretchWriteCursor) - targetReadDistanceSamples,
                          ringSizeF);

            float delta = desiredReadIndex - stretchReadIndexFloat;

            while (delta < -0.5f * ringSizeF)
                delta += ringSizeF;

            while (delta > 0.5f * ringSizeF)
                delta -= ringSizeF;

            stretchReadIndexFloat =
                wrapFloat(stretchReadIndexFloat + (0.02f * delta), ringSizeF);
        }
        else
        {
            underflowCount.fetch_add(1, std::memory_order_relaxed);
            outputSample = 0.0f;
        }

        // Debugging stuff
        const float currentReadWeight = GetCurrentReadWeight();

        float previousMin = debugMinReadWeight.load(std::memory_order_relaxed);
        if (currentReadWeight < previousMin)
            debugMinReadWeight.store(currentReadWeight, std::memory_order_relaxed);

        float previousMax = debugMaxReadWeight.load(std::memory_order_relaxed);
        if (currentReadWeight > previousMax)
            debugMaxReadWeight.store(currentReadWeight, std::memory_order_relaxed);

        if (!std::isfinite(outputSample))
            outputSample = 0.0f;

        return juce::jlimit(-2.0f, 2.0f, outputSample);
    }

    void OnEchoBoundary(float newRatio) override
    {
        const float clamped = clampPitchRatio(newRatio);

        if (std::abs(clamped - currentRatio) < 1.0e-6f)
            return;

        currentRatio = clamped;
        stretchFactor = currentRatio;

        smoothedReadIncrement = stretchFactor;

        samplesUntilNextSegment = std::max(
            1,
            static_cast<int>(std::round(
                static_cast<float>(synthesisHopSamples) / stretchFactor))
        );
    }

    void SetInitialRatio(float ratio) override
    {
        currentRatio = clampPitchRatio(ratio);
        stretchFactor = currentRatio;
        smoothedReadIncrement = stretchFactor;
    }

    float GetLatencyMilliseconds() const override
    {
        return (static_cast<float>(lookbackSamples) * 1000.0f)
            / static_cast<float>(sampleRate);
    }

    void SetSegmentLengthMilliseconds(float ms)
    {
        segmentLengthMs = std::clamp(ms, 8.0f, 60.0f);
        rebuildParametersFromTimeSettings();
        rebuildWindow();
    }

    void SetOverlapPercent(float percent)
    {
        overlapPercent = std::clamp(percent, 0.25f, 0.75f);
        rebuildParametersFromTimeSettings();
        rebuildWindow();
    }

    void SetSearchRadiusMilliseconds(float ms)
    {
        searchRadiusMs = std::clamp(ms, 0.5f, 10.0f);
        rebuildParametersFromTimeSettings();
    }

    void SetLookbackMilliseconds(float ms)
    {
        lookbackMs = std::clamp(ms, 20.0f, 200.0f);
        rebuildParametersFromTimeSettings();
    }

    int GetUnderflowCount() const
    {
        return underflowCount.load(std::memory_order_relaxed);
    }

    int GetCausalGuardRejectCount() const
    {
        return causalGuardRejectCount.load(std::memory_order_relaxed);
    }

    float GetLastBestMatchError() const
    {
        return lastBestMatchError.load(std::memory_order_relaxed);
    }

    float GetCurrentReadWeight() const
    {
        if (stretchWeightRing.empty() || stretchRingSize <= 0)
            return 0.0f;

        const int index =
            wrapInt(static_cast<int>(std::floor(stretchReadIndexFloat)), stretchRingSize);

        return stretchWeightRing[static_cast<size_t>(index)];
    }

    float GetAverageReadWeightWindow() const
    {
        if (stretchWeightRing.empty() || stretchRingSize <= 0)
            return 0.0f;

        const int center =
            wrapInt(static_cast<int>(std::floor(stretchReadIndexFloat)), stretchRingSize);

        float sum = 0.0f;
        constexpr int radius = 16;

        for (int offset = -radius; offset <= radius; ++offset)
        {
            const int idx = wrapInt(center + offset, stretchRingSize);
            sum += stretchWeightRing[static_cast<size_t>(idx)];
        }

        return sum / static_cast<float>((radius * 2) + 1);
    }

    float GetReadDistanceBehindWriteForDebug() const
    {
        return getReadDistanceBehindWrite();
    }

    int GetSamplesUntilNextSegmentForDebug() const
    {
        return samplesUntilNextSegment;
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

    int GetStretchWriteCursorForDebug() const
    {
        return stretchWriteCursor;
    }

    float GetStretchReadIndexForDebug() const
    {
        return stretchReadIndexFloat;
    }

    float GetDebugMinReadWeight() const
    {
        return debugMinReadWeight.load(std::memory_order_relaxed);
    }

    float GetDebugMaxReadWeight() const
    {
        return debugMaxReadWeight.load(std::memory_order_relaxed);
    }

    float GetTargetReadDistanceSamplesForDebug() const
    {
        return targetReadDistanceSamples;
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

    void ResetDebugReadWeightExtrema()
    {
        debugMinReadWeight.store(1000000.0f, std::memory_order_relaxed);
        debugMaxReadWeight.store(0.0f, std::memory_order_relaxed);
    }

private:
    void rebuildParametersFromTimeSettings()
    {
        segmentLengthSamples = std::max(
            64,
            static_cast<int>(std::round((segmentLengthMs * sampleRate) / 1000.0f))
        );

        overlapSamples = std::max(
            16,
            static_cast<int>(std::round(static_cast<float>(segmentLengthSamples) * overlapPercent))
        );

        overlapSamples = std::min(overlapSamples, segmentLengthSamples - 1);
        synthesisHopSamples = std::max(1, segmentLengthSamples - overlapSamples);

        searchRadiusSamples = std::max(
            8,
            static_cast<int>(std::round((searchRadiusMs * sampleRate) / 1000.0f))
        );

        lookbackSamples = std::max(
            segmentLengthSamples * 2,
            static_cast<int>(std::round((lookbackMs * sampleRate) / 1000.0f))
        );
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

    void clearBuffersAndState()
    {
        std::fill(inputRing.begin(), inputRing.end(), 0.0f);
        std::fill(stretchRing.begin(), stretchRing.end(), 0.0f);
        std::fill(stretchWeightRing.begin(), stretchWeightRing.end(), 0.0f);

        inputWriteIndex = 0;
        stretchWriteCursor = 0;
        stretchReadIndexFloat = 0.0f;
        stretchClearCursor = 0;
        smoothedReadIncrement = stretchFactor;

        samplesUntilNextSegment = 0;
        initialized = false;

        currentRatio = 1.0f;
        stretchFactor = 1.0f;
        predictedSourceIndex = 0.0f;
        lastChosenSourceIndex = 0.0f;

        startupSamplesReceived = 0;

        underflowCount.store(0, std::memory_order_relaxed);
        causalGuardRejectCount.store(0, std::memory_order_relaxed);
        lastBestMatchError.store(0.0f, std::memory_order_relaxed);
        debugMinReadWeight.store(1000000.0f, std::memory_order_relaxed);
        debugMaxReadWeight.store(0.0f, std::memory_order_relaxed);

        targetReadDistanceSamples = static_cast<float>(segmentLengthSamples * 3);
    }

    void tryInitializeIfReady()
    {
        const int requiredHistory =
            lookbackSamples + segmentLengthSamples + searchRadiusSamples;

        if (startupSamplesReceived < requiredHistory)
            return;

        const int seedIndex =
            wrapInt(inputWriteIndex - lookbackSamples, inputRingSize);

        predictedSourceIndex = static_cast<float>(seedIndex);
        lastChosenSourceIndex = predictedSourceIndex;

        for (int i = 0; i < 3; ++i)
            synthesizeNextSegment();

        samplesUntilNextSegment = synthesisHopSamples;
        stretchReadIndexFloat = static_cast<float>(overlapSamples / 2);
        initialized = true;
    }

    void synthesizeNextSegment()
    {
        if (segmentLengthSamples <= 0 || synthesisHopSamples <= 0)
            return;

        // WSOLA time-stretch stage:
        // synthesis hop is fixed in stretched domain
        // analysis hop depends on stretch factor
        const float analysisHop = static_cast<float>(synthesisHopSamples) / stretchFactor;

        const int predictedIndexInt =
            wrapInt(static_cast<int>(std::round(predictedSourceIndex)), inputRingSize);

        const int bestSourceIndex =
            findBestMatchingSourceIndex(predictedIndexInt);

        for (int i = 0; i < segmentLengthSamples; ++i)
        {
            const float sourceIndex =
                static_cast<float>(bestSourceIndex) + static_cast<float>(i);

            const float sample = readInputRingLinear(sourceIndex);
            const float weight = window[static_cast<size_t>(i)];
            const float weightedSample = sample * weight;

            const int stretchIndex =
                wrapInt(stretchWriteCursor + i, stretchRingSize);

            stretchRing[static_cast<size_t>(stretchIndex)] += weightedSample;
            stretchWeightRing[static_cast<size_t>(stretchIndex)] += weight;
        }

        stretchWriteCursor =
            wrapInt(stretchWriteCursor + synthesisHopSamples, stretchRingSize);

        lastChosenSourceIndex = static_cast<float>(bestSourceIndex);

        const float freeRunningPrediction =
            wrapFloat(lastChosenSourceIndex + analysisHop,
                      static_cast<float>(inputRingSize));

        const float nominalLookbackPrediction =
            wrapFloat(static_cast<float>(inputWriteIndex - lookbackSamples),
                      static_cast<float>(inputRingSize));

        // Gently pull predictor back toward the nominal causally-safe region.
        const float pull = 0.1f;

        // Compute shortest wrapped difference
        float delta = nominalLookbackPrediction - freeRunningPrediction;
        const float ringSizeF = static_cast<float>(inputRingSize);

        while (delta < -0.5f * ringSizeF)
            delta += ringSizeF;

        while (delta > 0.5f * ringSizeF)
            delta -= ringSizeF;

        predictedSourceIndex =
            wrapFloat(freeRunningPrediction + (pull * delta), ringSizeF);
    }

int findBestMatchingSourceIndex(int predictedSourceIndexSamples) const
{
    float bestScore = -std::numeric_limits<float>::max();
    int bestIndex = predictedSourceIndexSamples;
    bool foundValidCandidate = false;

    // Optional continuity bias. Keep disabled until matching is confirmed useful.
    constexpr float distancePenaltyPerSample = 0.0f;

    // Compare each candidate against the tail region implied by the previously
    // chosen source segment, rather than against the stretch ring.
    const float referenceStart =
        lastChosenSourceIndex
        + static_cast<float>(segmentLengthSamples - overlapSamples);

    for (int delta = -searchRadiusSamples; delta <= searchRadiusSamples; ++delta)
    {
        const int candidateIndex =
            wrapInt(predictedSourceIndexSamples + delta, inputRingSize);

        if (!isCandidateCausallySafe(candidateIndex))
        {
            causalGuardRejectCount.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

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
        else
            lastBestMatchDelta.store(0, std::memory_order_relaxed);
    }

    if (!foundValidCandidate)
    {
        lastBestMatchError.store(0.0f, std::memory_order_relaxed);
        return predictedSourceIndexSamples;
    }

    if (!std::isfinite(bestScore))
        bestScore = -1.0f;

    lastBestMatchError.store(bestScore, std::memory_order_relaxed);

    return bestIndex;
}

    bool isCandidateCausallySafe(int candidateIndex) const
    {
        const int candidateEnd = wrapInt(candidateIndex + segmentLengthSamples - 1, inputRingSize);

        const int distanceToWrite =
            positiveDistanceForward(candidateEnd, inputWriteIndex, inputRingSize);

        constexpr int guardSamples = 8;
        return distanceToWrite >= guardSamples;
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

    float readStretchRingNormalizedCubic(float index) const
    {
        const float wrapped = wrapFloat(index, static_cast<float>(stretchRingSize));

        const int i1 = static_cast<int>(std::floor(wrapped));
        const float frac = wrapped - static_cast<float>(i1);

        const int i0 = wrapInt(i1 - 1, stretchRingSize);
        const int i2 = wrapInt(i1 + 1, stretchRingSize);
        const int i3 = wrapInt(i1 + 2, stretchRingSize);

        auto getNormalized = [&](int idx) -> float
        {
            const float sum = stretchRing[static_cast<size_t>(idx)];
            const float weight = stretchWeightRing[static_cast<size_t>(idx)];
            const float safeWeight = std::max(weight, 1.0e-3f);
            return sum / safeWeight;
        };

        const float y0 = getNormalized(i0);
        const float y1 = getNormalized(i1);
        const float y2 = getNormalized(i2);
        const float y3 = getNormalized(i3);

        const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a2 = -0.5f * y0 + 0.5f * y2;
        const float a3 = y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

    void reclaimOldStretchData()
    {
    }

    float getReadDistanceBehindWrite() const
    {
        const float writePos = static_cast<float>(stretchWriteCursor);
        float distance = writePos - stretchReadIndexFloat;

        while (distance < 0.0f)
            distance += static_cast<float>(stretchRingSize);

        while (distance >= static_cast<float>(stretchRingSize))
            distance -= static_cast<float>(stretchRingSize);

        return distance;
    }

    float getControlledReadIncrement() const
    {
        const float distance = getReadDistanceBehindWrite();
        const float error = distance - targetReadDistanceSamples;

        // Small proportional correction only.
        const float correction = juce::jlimit(-0.005f, 0.005f, error * 0.0001f);

        return stretchFactor + correction;
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

    // Runtime configuration
    double sampleRate = 48000.0;
    int maximumBlockSizeCached = 0;

    // User-tunable timing settings
    float segmentLengthMs = 18.0f;
    float overlapPercent = 0.60f;
    float searchRadiusMs = 2.0f;
    float lookbackMs = 60.0f;

    // Derived sample counts
    int segmentLengthSamples = 960;
    int overlapSamples = 576;
    int synthesisHopSamples = 384;
    int searchRadiusSamples = 96;
    int lookbackSamples = 2880;

    // Buffers
    std::vector<float> inputRing;

    // Intermediate WSOLA time-stretch output
    std::vector<float> stretchRing;
    std::vector<float> stretchWeightRing;

    std::vector<float> window;

    int inputRingSize = 0;
    int stretchRingSize = 0;

    // Input / stretch cursors
    int inputWriteIndex = 0;
    int stretchWriteCursor = 0;
    float stretchReadIndexFloat = 0.0f;
    int stretchClearCursor = 0;
    float targetReadDistanceSamples = 0.0f;
    float smoothedReadIncrement = 1.0f;

    // Segment scheduling
    int samplesUntilNextSegment = 0;
    bool initialized = false;

    // Ratio / source tracking
    float currentRatio = 1.0f;
    float stretchFactor = 1.0f;
    float predictedSourceIndex = 0.0f;
    float lastChosenSourceIndex = 0.0f;

    // Startup tracking
    int startupSamplesReceived = 0;

    // Debug state
    mutable std::atomic<int> underflowCount { 0 };
    mutable std::atomic<int> causalGuardRejectCount { 0 };
    mutable std::atomic<float> lastBestMatchError { 0.0f };
    mutable std::atomic<float> debugMinReadWeight { 1000000.0f };
    mutable std::atomic<float> debugMaxReadWeight { 0.0f };
    mutable std::atomic<int> lastBestMatchDelta { 0 };
};