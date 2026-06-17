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

private:
    void rebuildStageDelays();
    void updateStageGains();

    double sampleRate = 48000.0;
    int activeStages = MaxStages;

    float diffusionAmount = 0.0f;
    float size01 = 1.0f;
    float totalChainDelayMs = 0.0f;

    static constexpr float MaxAllpassGain = 0.58f;

    const std::array<float, MaxStages> stageTuningsMs =
    {
        7.0f, 11.0f, 16.0f, 24.0f, 36.0f, 54.0f, 81.0f, 122.0f
    };

    std::array<DiffusionAllpass, MaxStages> allpasses {};
};