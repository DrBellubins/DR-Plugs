#pragma once

// =====================================================================================
// WSOLAPitchBackend
//
// Minimal real-time WSOLA-style pitch shifter for Chronoverb.
//
// Design goals:
// - Fits IPitchShifterBackend.
// - Real-time safe: no allocations or locks in ProcessSample().
// - Pitch changes occur only at echo boundaries via OnEchoBoundary().
// - Optimized for preserving sharper delay taps better than a classic phase vocoder.
// - Uses bounded waveform-similarity search with overlap-add synthesis.
//
// Algorithm sketch:
// - Input is written into a circular history buffer.
// - Output is synthesized in segment-sized chunks into an output ring.
// - For each new segment, we predict the next source location based on pitch ratio.
// - We search a small region around that prediction for the segment whose overlap
//   best matches the already-synthesized output tail.
// - We window the chosen segment into the output ring and normalize by accumulated
//   window weights during readout.
//
// This is intentionally a minimal, practical WSOLA backend rather than a full
// commercial-grade timestretcher.
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

        rebuildParametersFromTimeSettings();

        // Input history needs enough room for:
        // - configured lookback
        // - full segment
        // - search radius on both sides
        // - extra safety margin
        inputRingSize = std::max(
            8192,
            lookbackSamples + segmentLengthSamples + (searchRadiusSamples * 2) + 2048
        );

        // Stretch ring must hold several synthesized segments plus safe read/write separation
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

    // pitchRatio is ignored during sample processing. Ratio changes are quantized
    // externally and committed via OnEchoBoundary() / SetInitialRatio().
    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        if (inputRing.empty() || stretchRing.empty() || stretchWeightRing.empty())
            return inputSample;

        // 1) Write input into source history ring
        inputRing[static_cast<size_t>(inputWriteIndex)] = inputSample;
        inputWriteIndex = wrapInt(inputWriteIndex + 1, inputRingSize);

        if (startupSamplesReceived < std::numeric_limits<int>::max())
            ++startupSamplesReceived;

        // 2) Initialize once enough history exists
        if (!initialized)
            tryInitializeIfReady();

        // 3) Continuously synthesize time-stretched segments
        if (initialized)
        {
            --samplesUntilNextSegment;

            if (samplesUntilNextSegment <= 0)
            {
                synthesizeNextSegment();
                samplesUntilNextSegment = synthesisHopSamples;
            }
        }

        // 4) Resample stretched intermediate back to real-time output
        float outputSample = 0.0f;

        if (initialized)
        {
            outputSample = readStretchRingNormalizedLinear(stretchReadIndexFloat);

            const int clearIndex = wrapInt(
                static_cast<int>(std::floor(stretchReadIndexFloat)),
                stretchRingSize);

            clearConsumedStretchSample(clearIndex);

            stretchReadIndexFloat =
                wrapFloat(stretchReadIndexFloat + stretchFactor,
                          static_cast<float>(stretchRingSize));
        }
        else
        {
            underflowCount.fetch_add(1, std::memory_order_relaxed);
        }

        if (!std::isfinite(outputSample))
            outputSample = 0.0f;

        return juce::jlimit(-2.0f, 2.0f, outputSample);
    }

    void OnEchoBoundary(float newRatio) override
    {
        currentRatio = clampPitchRatio(newRatio);
        stretchFactor = 1.0f / currentRatio;
    }

    void SetInitialRatio(float ratio) override
    {
        currentRatio = clampPitchRatio(ratio);
        stretchFactor = 1.0f / currentRatio;
    }

    float GetLatencyMilliseconds() const override
    {
        return (static_cast<float>(lookbackSamples) * 1000.0f)
            / static_cast<float>(sampleRate);
    }

    // Optional tuning API
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

    // Debug counters
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

        synthesizeNextSegment();
        samplesUntilNextSegment = synthesisHopSamples;

        // Start reading from the beginning of the synthesized stretch ring.
        stretchReadIndexFloat = 0.0f;

        initialized = true;
    }

    void synthesizeNextSegment()
    {
        if (segmentLengthSamples <= 0 || synthesisHopSamples <= 0)
            return;

        // WSOLA time-stretch stage:
        // synthesis hop is fixed in stretched domain
        // analysis hop depends on stretch factor
        const float analysisHop =
            static_cast<float>(synthesisHopSamples) * stretchFactor;

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
        predictedSourceIndex = lastChosenSourceIndex + analysisHop;
    }

    int findBestMatchingSourceIndex(int predictedSourceIndexSamples) const
    {
        float bestError = std::numeric_limits<float>::max();
        int bestIndex = predictedSourceIndexSamples;

        const int overlapStretchStart = stretchWriteCursor;

        for (int delta = -searchRadiusSamples; delta <= searchRadiusSamples; ++delta)
        {
            const int candidateIndex =
                wrapInt(predictedSourceIndexSamples + delta, inputRingSize);

            if (!isCandidateCausallySafe(candidateIndex))
            {
                causalGuardRejectCount.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            float error = 0.0f;

            for (int i = 0; i < overlapSamples; ++i)
            {
                const float candidateSample =
                    readInputRingLinear(static_cast<float>(candidateIndex + i));

                const int stretchIndex =
                    wrapInt(overlapStretchStart + i, stretchRingSize);

                const float existingWeight =
                    stretchWeightRing[static_cast<size_t>(stretchIndex)];

                float existingSample = 0.0f;
                if (existingWeight > 1.0e-6f)
                {
                    existingSample =
                        stretchRing[static_cast<size_t>(stretchIndex)] / existingWeight;
                }

                const float weightedCandidate =
                    candidateSample * window[static_cast<size_t>(i)];

                const float diff = existingSample - weightedCandidate;
                error += diff * diff;
            }

            if (error < bestError)
            {
                bestError = error;
                bestIndex = candidateIndex;
            }
        }

        if (!std::isfinite(bestError))
            bestError = 0.0f;

        lastBestMatchError.store(bestError, std::memory_order_relaxed);
        return bestIndex;
    }

    bool isCandidateCausallySafe(int candidateIndex) const
    {
        // Candidate segment must fully exist in past input history.
        // We reject any segment whose end is too close to / ahead of the input write pointer.
        const int candidateEnd = wrapInt(candidateIndex + segmentLengthSamples - 1, inputRingSize);

        // Distance forward from candidateEnd to inputWriteIndex tells us how far "behind"
        // the write head the candidate is. Need at least a small guard margin.
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

    float readStretchRingNormalizedLinear(float index) const
    {
        const float wrapped = wrapFloat(index, static_cast<float>(stretchRingSize));

        const int indexA = static_cast<int>(std::floor(wrapped));
        const int indexB = wrapInt(indexA + 1, stretchRingSize);
        const float frac = wrapped - static_cast<float>(indexA);

        const float sumA = stretchRing[static_cast<size_t>(indexA)];
        const float sumB = stretchRing[static_cast<size_t>(indexB)];

        const float weightA = stretchWeightRing[static_cast<size_t>(indexA)];
        const float weightB = stretchWeightRing[static_cast<size_t>(indexB)];

        float sampleA = 0.0f;
        float sampleB = 0.0f;

        if (weightA > 1.0e-6f)
            sampleA = sumA / weightA;

        if (weightB > 1.0e-6f)
            sampleB = sumB / weightB;

        return sampleA + frac * (sampleB - sampleA);
    }

    void clearConsumedStretchSample(int index)
    {
        stretchRing[static_cast<size_t>(index)] = 0.0f;
        stretchWeightRing[static_cast<size_t>(index)] = 0.0f;
    }

private:
    // Runtime configuration
    double sampleRate = 48000.0;
    int maximumBlockSizeCached = 0;

    // User-tunable timing settings (stored in milliseconds/percent)
    float segmentLengthMs = 20.0f;
    float overlapPercent = 0.5f;
    float searchRadiusMs = 3.0f;
    float lookbackMs = 80.0f;

    // Derived sample counts
    int segmentLengthSamples = 960;
    int overlapSamples = 480;
    int synthesisHopSamples = 480;
    int searchRadiusSamples = 144;
    int lookbackSamples = 3840;

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
};