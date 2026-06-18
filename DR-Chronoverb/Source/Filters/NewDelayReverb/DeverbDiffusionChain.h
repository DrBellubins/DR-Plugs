#pragma once

#include <array>
#include <algorithm>
#include <cmath>

#include "DiffusionAllpass.h"

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

    void SetStageGain(int index, float newGain);
    void SetStageGains(float maxGain, std::array<float, MaxStages> stageGains);

    float ProcessSample(float inputSample);

    float GetTotalChainDelayMs() const { return totalChainDelayMs; }
    float GetTotalTuningMs() const;

private:
    void rebuildStageDelays();

    double sampleRate = 48000.0;
    int activeStages = MaxStages;

    float diffusionAmount = 0.0f;
    float size01 = 1.0f;
    float totalChainDelayMs = 0.0f;
    float totalTuningMs = 0.0f;

    std::array<float, MaxStages> stageTuningsMs = {};

    std::array<DiffusionAllpass, MaxStages> allpasses {};
};