#include "WSOLAPitchBackend.h"

#include <juce_core/juce_core.h>

void WSOLAPitchBackend::Prepare(double newSampleRate, int maximumBlockSize)
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

    // Output ring needs enough room for:
    // - several overlapping segments
    // - worst-case scheduling jitter between synthesis and sample consumption
    outputRingSize = std::max(
        16384,
        segmentLengthSamples * 12
    );

    inputRing.assign(static_cast<size_t>(inputRingSize), 0.0f);
    outputRing.assign(static_cast<size_t>(outputRingSize), 0.0f);
    outputWeightRing.assign(static_cast<size_t>(outputRingSize), 0.0f);

    rebuildWindow();
    clearBuffersAndState();
}

void WSOLAPitchBackend::Reset()
{
    clearBuffersAndState();
}

float WSOLAPitchBackend::ProcessSample(float inputSample, float /*pitchRatio*/)
{
    if (inputRing.empty() || outputRing.empty() || outputWeightRing.empty())
        return inputSample;

    // 1) Write input sample to history ring.
    inputRing[static_cast<size_t>(inputWriteIndex)] = inputSample;
    inputWriteIndex = wrapInt(inputWriteIndex + 1, inputRingSize);

    if (startupSamplesReceived < std::numeric_limits<int>::max())
        ++startupSamplesReceived;

    // 2) Initialize once we have enough lookback history.
    if (!initialized)
        tryInitializeIfReady();

    // 3) Schedule new synthesized segments at fixed synthesis hop spacing.
    if (initialized)
    {
        --samplesUntilNextSegment;

        if (samplesUntilNextSegment <= 0)
        {
            synthesizeNextSegment();
            samplesUntilNextSegment = synthesisHopSamples;
        }
    }

    // 4) Emit one sample from normalized output ring.
    const float outputSample = readOutputNormalizedAndClear(outputReadIndex);
    outputReadIndex = wrapInt(outputReadIndex + 1, outputRingSize);

    return outputSample;
}

void WSOLAPitchBackend::OnEchoBoundary(float newRatio)
{
    currentRatio = clampPitchRatio(newRatio);
}

void WSOLAPitchBackend::SetInitialRatio(float ratio)
{
    currentRatio = clampPitchRatio(ratio);
}

float WSOLAPitchBackend::GetLatencyMilliseconds() const
{
    return (static_cast<float>(lookbackSamples) * 1000.0f)
        / static_cast<float>(sampleRate);
}

void WSOLAPitchBackend::SetSegmentLengthMilliseconds(float ms)
{
    segmentLengthMs = std::clamp(ms, 8.0f, 60.0f);
    rebuildParametersFromTimeSettings();
    rebuildWindow();
}

void WSOLAPitchBackend::SetOverlapPercent(float percent)
{
    overlapPercent = std::clamp(percent, 0.25f, 0.75f);
    rebuildParametersFromTimeSettings();
    rebuildWindow();
}

void WSOLAPitchBackend::SetSearchRadiusMilliseconds(float ms)
{
    searchRadiusMs = std::clamp(ms, 0.5f, 10.0f);
    rebuildParametersFromTimeSettings();
}

void WSOLAPitchBackend::SetLookbackMilliseconds(float ms)
{
    lookbackMs = std::clamp(ms, 20.0f, 200.0f);
    rebuildParametersFromTimeSettings();
}

int WSOLAPitchBackend::GetUnderflowCount() const
{
    return underflowCount.load(std::memory_order_relaxed);
}

int WSOLAPitchBackend::GetCausalGuardRejectCount() const
{
    return causalGuardRejectCount.load(std::memory_order_relaxed);
}

float WSOLAPitchBackend::GetLastBestMatchError() const
{
    return lastBestMatchError.load(std::memory_order_relaxed);
}

void WSOLAPitchBackend::rebuildParametersFromTimeSettings()
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

void WSOLAPitchBackend::rebuildWindow()
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

void WSOLAPitchBackend::clearBuffersAndState()
{
    std::fill(inputRing.begin(), inputRing.end(), 0.0f);
    std::fill(outputRing.begin(), outputRing.end(), 0.0f);
    std::fill(outputWeightRing.begin(), outputWeightRing.end(), 0.0f);

    inputWriteIndex = 0;
    outputReadIndex = 0;
    outputWriteCursor = 0;

    samplesUntilNextSegment = 0;
    initialized = false;

    predictedSourceIndex = 0.0f;
    lastChosenSourceIndex = 0.0f;

    startupSamplesReceived = 0;

    underflowCount.store(0, std::memory_order_relaxed);
    causalGuardRejectCount.store(0, std::memory_order_relaxed);
    lastBestMatchError.store(0.0f, std::memory_order_relaxed);
}

void WSOLAPitchBackend::tryInitializeIfReady()
{
    // Need enough history before we can safely synthesize a segment.
    const int requiredHistory = lookbackSamples + segmentLengthSamples + searchRadiusSamples;

    if (startupSamplesReceived < requiredHistory)
        return;

    // Seed first source position safely behind the write pointer.
    const int seedIndex =
        wrapInt(inputWriteIndex - lookbackSamples, inputRingSize);

    predictedSourceIndex = static_cast<float>(seedIndex);
    lastChosenSourceIndex = predictedSourceIndex;

    // Seed an initial segment immediately so output starts from valid material.
    synthesizeNextSegment();
    samplesUntilNextSegment = synthesisHopSamples;
    initialized = true;
}

void WSOLAPitchBackend::synthesizeNextSegment()
{
    if (segmentLengthSamples <= 0 || synthesisHopSamples <= 0)
        return;

    // Predicted source advance:
    // fixed synthesis hop, variable source hop by inverse ratio.
    const float analysisHop =
        static_cast<float>(synthesisHopSamples) / currentRatio;

    const int predictedIndexInt =
        wrapInt(static_cast<int>(std::round(predictedSourceIndex)), inputRingSize);

    const int bestSourceIndex = findBestMatchingSourceIndex(predictedIndexInt);

    // Window + overlap-add into output ring
    for (int i = 0; i < segmentLengthSamples; ++i)
    {
        const float sourceIndex =
            static_cast<float>(bestSourceIndex) + static_cast<float>(i);

        const float sample = readInputRingLinear(sourceIndex);
        const float weight = window[static_cast<size_t>(i)];
        const float weightedSample = sample * weight;

        const int outIndex = wrapInt(outputWriteCursor + i, outputRingSize);
        addToOutputRing(outIndex, weightedSample, weight);
    }

    outputWriteCursor = wrapInt(outputWriteCursor + synthesisHopSamples, outputRingSize);

    lastChosenSourceIndex = static_cast<float>(bestSourceIndex);
    predictedSourceIndex = lastChosenSourceIndex + analysisHop;
}

int WSOLAPitchBackend::findBestMatchingSourceIndex(int predictedSourceIndex) const
{
    float bestError = std::numeric_limits<float>::max();
    int bestIndex = predictedSourceIndex;

    // Compare candidate overlap against already-synthesized output beginning
    // at the current write cursor.
    const int overlapOutputStart = outputWriteCursor;

    for (int delta = -searchRadiusSamples; delta <= searchRadiusSamples; ++delta)
    {
        const int candidateIndex =
            wrapInt(predictedSourceIndex + delta, inputRingSize);

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

            const int outIndex = wrapInt(overlapOutputStart + i, outputRingSize);

            // Compare against normalized existing output tail.
            const float existingWeight = outputWeightRing[static_cast<size_t>(outIndex)];
            float existingOutput = 0.0f;

            if (existingWeight > 1.0e-6f)
                existingOutput = outputRing[static_cast<size_t>(outIndex)] / existingWeight;

            const float weightedCandidate =
                candidateSample * window[static_cast<size_t>(i)];

            const float diff = existingOutput - weightedCandidate;
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

bool WSOLAPitchBackend::isCandidateCausallySafe(int candidateIndex) const
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

float WSOLAPitchBackend::readInputRingLinear(float index) const
{
    const float wrapped = wrapFloat(index, static_cast<float>(inputRingSize));

    const int indexA = static_cast<int>(std::floor(wrapped));
    const int indexB = wrapInt(indexA + 1, inputRingSize);
    const float frac = wrapped - static_cast<float>(indexA);

    const float a = inputRing[static_cast<size_t>(indexA)];
    const float b = inputRing[static_cast<size_t>(indexB)];

    return a + frac * (b - a);
}

float WSOLAPitchBackend::readOutputNormalizedAndClear(int index)
{
    const float sum = outputRing[static_cast<size_t>(index)];
    const float weight = outputWeightRing[static_cast<size_t>(index)];

    float out = 0.0f;

    if (weight > 1.0e-6f)
    {
        out = sum / weight;
    }
    else
    {
        underflowCount.fetch_add(1, std::memory_order_relaxed);
        out = 0.0f;
    }

    if (!std::isfinite(out))
        out = 0.0f;

    out = juce::jlimit(-2.0f, 2.0f, out);

    outputRing[static_cast<size_t>(index)] = 0.0f;
    outputWeightRing[static_cast<size_t>(index)] = 0.0f;

    return out;
}

void WSOLAPitchBackend::addToOutputRing(int index, float sample, float weight)
{
    outputRing[static_cast<size_t>(index)] += sample;
    outputWeightRing[static_cast<size_t>(index)] += weight;
}

float WSOLAPitchBackend::clampPitchRatio(float ratio)
{
    return std::clamp(ratio, 0.25f, 4.0f);
}

int WSOLAPitchBackend::wrapInt(int value, int size)
{
    while (value < 0)
        value += size;

    while (value >= size)
        value -= size;

    return value;
}

float WSOLAPitchBackend::wrapFloat(float value, float size)
{
    while (value < 0.0f)
        value += size;

    while (value >= size)
        value -= size;

    return value;
}

int WSOLAPitchBackend::positiveDistanceForward(int fromIndex, int toIndex, int size)
{
    int distance = toIndex - fromIndex;

    while (distance < 0)
        distance += size;

    return distance;
}