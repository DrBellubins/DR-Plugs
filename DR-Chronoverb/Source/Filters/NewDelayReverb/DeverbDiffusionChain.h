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

    void Prepare(double newSampleRate);
    void Reset();

    void SetQuality(int newStageCount);
    void SetSize(float newSize01);
    void SetDiffusionAmount(float newAmount01);

    float ProcessSample(float inputSample);

    float GetBlendAmount() const;
    float GetTotalChainDelayMs() const { return totalChainDelayMs; }

    float GetTotalTuningMs() const;

private:
    void rebuildStageDelays();
    void updateStageGains();

    double sampleRate = 48000.0;
    int activeStages = MaxStages;

    float diffusionAmount = 0.0f;
    float size01 = 1.0f;
    float totalChainDelayMs = 0.0f;
    float totalTuningMs = 0.0f;

    static constexpr float MaxAllpassGain = 0.58f;

    const std::array<float, MaxStages> stageTuningsMs =
    {
        5.0, 11.0, 19.0, 31.0, 43.0, 53.0, 73.0, 83.0
        //11.0f, 13.0f, 23.0f, 31.0f, 43.0f, 53.0f, 73.0f, 83.0f
    };

    std::array<DiffusionAllpass, MaxStages> allpasses {};
};