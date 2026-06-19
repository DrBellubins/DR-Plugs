#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <random>

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

    void Prepare(double newSampleRate, std::array<float, MaxStages> stageTunings);
    void Reset();

    void SetQuality(int newStageCount);
    void SetSize(float newSize01);
    void SetDiffusionAmount(float newAmount01);

    void SetStageGains(float baseGain, std::array<float, MaxStages> stageGains);

    void SetJitterRate(float newRateHz);
    void SetJitterDepth(float newDepthMs);

    float ProcessSample(float inputSample);

    [[nodiscard]] float GetTotalChainDelayMs() const { return totalChainDelayMs; }
    [[nodiscard]] float GetTotalTuningMs() const;

private:
    void rebuildStageDelays();
    void updateJitterTargets();
    void pushJitterTargetsToAllpasses();

    double sampleRate = 48000.0;
    size_t activeStages = MaxStages;

    float diffusionAmount = 0.0f;
    float size01 = 1.0f;
    float totalChainDelayMs = 0.0f;
    float totalTuningMs = 0.0f;

    // Gain smoothing
    std::array<float, MaxStages> currentStageGains {};
    std::array<float, MaxStages> targetStageGains {};

    float currentBaseGain = 0.0f;
    float targetBaseGain = 0.0f;
    float gainSlewCoefficient = 0.0f;

    // Jitter
    float jitterRateHz = 0.5f;
    float jitterDepthMs = 0.35f;
    float jitterAlpha = 0.0f;
    int jitterIntervalSamples = 1;
    int jitterCountdown = 1;

    std::array<float, MaxStages> jitterTargets {};
    std::array<float, MaxStages> jitterSmoothedOffsets {};
    std::mt19937 rng { 0x12345678 };
    std::uniform_real_distribution<float> jitterDist { -1.0f, 1.0f };

    std::array<float, MaxStages> stageTuningsMs = {};

    std::array<DeverbDiffusionAllpass, MaxStages> allpasses {};
};