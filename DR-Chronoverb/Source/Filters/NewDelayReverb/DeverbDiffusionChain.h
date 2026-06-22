#pragma once

#include <array>
#include <algorithm>
#include <cmath>

#include "DeverbDiffusionAllpass.h"

// A dedicated diffusion chain for the Deverb experiment.
// This version keeps the experiment intact while ensuring that
// diffusionAmount does NOT modulate any time-domain read position.
// Amount only controls:
// 1) per-stage allpass gain
// 2) clean/diffused write blend amount
class DeverbDiffusionChain
{
public:
    static constexpr int MaxStages = 8;

    void Prepare(double newSampleRate, std::array<float, MaxStages> stageTunings,
        float jitterRate, float jitterDepth);

    void Reset();

    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newStageCount);

    void SetStageGains(float baseGain, std::array<float, MaxStages> stageGains);

    float ProcessSample(float inputSample);

    [[nodiscard]] float GetTotalChainDelayMs() const { return totalChainDelayMs; }
    [[nodiscard]] float GetTotalTuningMs() const;

private:
    void rebuildStageDelays();
    [[nodiscard]] std::array<float, MaxStages> buildDistributedTunings(size_t outputStages) const;

    static std::array<float, MaxStages> buildDistributedGains(
        const std::array<float, MaxStages>& source,
        size_t outputStages);

    void initializeDriftState();
    void retargetStageDrift(size_t stageIndex);
    void updateStageJitterDepths();
    [[nodiscard]] float getQualityJitterScale() const;
    [[nodiscard]] float getAmountJitterScale() const;

    double sampleRate = 48000.0;
    size_t activeStages = MaxStages;

    float diffusionAmount = 0.0f;
    float size01 = 1.0f;
    float totalChainDelayMs = 0.0f;
    float totalTuningMs = 0.0f;

    float jitterLfoRate = 0.0f;
    float jitterLfoDepth = 0.0f;

    // Gain smoothing
    std::array<float, MaxStages> currentStageGains {};
    std::array<float, MaxStages> targetStageGains {};

    float currentBaseGain = 0.0f;
    float targetBaseGain = 0.0f;
    float gainSlewCoefficient = 0.0f;

    // Compensation
    float targetQualityCompensation = 1.0f;

    // Per-stage smooth random drift state
    std::array<float, MaxStages> driftCurrentMs {};
    std::array<float, MaxStages> driftTargetMs {};
    std::array<float, MaxStages> driftStepPerSample {};
    std::array<int,   MaxStages> driftSamplesUntilRetarget {};

    // Per-stage depth scaling derived from stage tuning length
    std::array<float, MaxStages> stageJitterDepthMs {};

    // Random source for aperiodic modulation
    juce::Random random;

    // Base retarget timing for the drift system
    float driftRetargetMinMs = 120.0f;
    float driftRetargetMaxMs = 380.0f;

    // Optional global safety trim
    float jitterGlobalDepthScale = 0.25f;

    // Envelope (for LFO termination)
    float chainEnvelope = 0.0f;
    static constexpr float EnvelopeAttackCoeff  = 0.9999f; // near-instant attack
    static constexpr float EnvelopeReleaseCoeff = 0.9999f; // ~sample-accurate tracking
    static constexpr float LfoGateThreshold     = 1.0e-6f; // below this, suppress LFO offset

    std::array<float, MaxStages> stageTuningsMs = {};
    std::array<DeverbDiffusionAllpass, MaxStages> allpasses {};

    std::array<float, MaxStages> distributedTuningsMs{};
    std::array<float, MaxStages> distributedGainMultipliers {};
};