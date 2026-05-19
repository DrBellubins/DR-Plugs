#pragma once

#include <vector>
#include <atomic>
#include <limits>
#include <cmath>
#include <algorithm>

#include "PitchShiftingUtils.h"

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

    void Prepare(double newSampleRate, int maximumBlockSize) override;
    void Reset() override;

    // pitchRatio is ignored during sample processing. Ratio changes are quantized
    // externally and committed via OnEchoBoundary() / SetInitialRatio().
    float ProcessSample(float inputSample, float pitchRatio) override;

    void OnEchoBoundary(float newRatio) override;
    void SetInitialRatio(float ratio) override;
    float GetLatencyMilliseconds() const override;

    // Optional tuning API
    void SetSegmentLengthMilliseconds(float ms);
    void SetOverlapPercent(float percent);
    void SetSearchRadiusMilliseconds(float ms);
    void SetLookbackMilliseconds(float ms);

    // Debug counters
    int GetUnderflowCount() const;
    int GetCausalGuardRejectCount() const;
    float GetLastBestMatchError() const;

private:
    void rebuildParametersFromTimeSettings();
    void rebuildWindow();
    void clearBuffersAndState();

    void tryInitializeIfReady();
    void synthesizeNextSegment();

    int findBestMatchingSourceIndex(int predictedSourceIndex) const;
    bool isCandidateCausallySafe(int candidateIndex) const;

    float readInputRingLinear(float index) const;
    float readOutputNormalizedAndClear(int index);

    void addToOutputRing(int index, float sample, float weight);

    static float clampPitchRatio(float ratio);
    static int wrapInt(int value, int size);
    static float wrapFloat(float value, float size);
    static int positiveDistanceForward(int fromIndex, int toIndex, int size);

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
    std::vector<float> outputRing;
    std::vector<float> outputWeightRing;
    std::vector<float> window;

    int inputRingSize = 0;
    int outputRingSize = 0;

    // Input/output cursors
    int inputWriteIndex = 0;
    int outputReadIndex = 0;
    int outputWriteCursor = 0;

    // Segment scheduling
    int samplesUntilNextSegment = 0;
    bool initialized = false;

    // Ratio / source tracking
    float currentRatio = 1.0f;
    float predictedSourceIndex = 0.0f;
    float lastChosenSourceIndex = 0.0f;

    // Startup tracking
    int startupSamplesReceived = 0;

    // Debug state
    mutable std::atomic<int> underflowCount { 0 };
    mutable std::atomic<int> causalGuardRejectCount { 0 };
    std::atomic<float> lastBestMatchError { 0.0f };
};