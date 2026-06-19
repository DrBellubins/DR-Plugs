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

    // Jitter LFO modulation
    static constexpr float LfoBaseRateHz = 0.15f;  // Slow drift
    static constexpr float LfoDepthMs = 0.25f;     // Subtle — just enough to spread comb notches

    std::array<float, MaxStages> lfoPhases {};      // Per-stage phase (radians)
    std::array<float, MaxStages> lfoRates  {};      // Per-stage rate (radians/sample) — set in Prepare

    std::array<float, MaxStages> stageTuningsMs = {};

    std::array<DeverbDiffusionAllpass, MaxStages> allpasses {};
};