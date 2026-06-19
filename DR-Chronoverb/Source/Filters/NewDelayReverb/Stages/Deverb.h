#pragma once

#include <memory>
#include <utility>
#include <algorithm>
#include <cmath>

#include "Filters.h"
#include "../DelayLine.h"
#include "../DampingFilter.h"
#include "../DelayTimeSegment.h"
#include "../DeverbDiffusionChain.h"
#include "../../../Utils/PMath.h"

// TODO: Diffusion quality doesn't interpolate tunings (ugh)
// TODO: Make diff amt 1 respond sooner (needs research)
// TODO: Simply boosting shorter all-pass gains didn't work.

// TODO: Add jitter in DeverbDiffusionChain

class Deverb
{
public:
    static constexpr float MaxAllpassGain = 0.58f;

    const std::array<float, DeverbDiffusionChain::MaxStages> AllpassTunings =
    {
        5.0, 11.0, 19.0, 31.0, 43.0, 53.0, 73.0, 83.0
        //11.0f, 13.0f, 23.0f, 31.0f, 43.0f, 53.0f, 73.0f, 83.0f
    };

    // Multiplier of MaxAllpassGain
    const std::array<float, DeverbDiffusionChain::MaxStages> DelayAllpassGainMultipliers =
    {
        1.0, 1.0, 1.0, 1.0, 0.5, 0.0, 0.0, 0.0
    };

    // Raw multiplier
    const std::array<float, DeverbDiffusionChain::MaxStages> ReverbAllpassGainMultipliers =
    {
        2.0, 1.5, 1.0, MaxAllpassGain, MaxAllpassGain, MaxAllpassGain, MaxAllpassGain, MaxAllpassGain
    };

    void PrepareToPlay(double newSampleRate, Filters& filters);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    std::pair<float, float> ProcessSample(float inputSampleL, float inputSampleR);

    void Reset();

    std::pair<DelayLine&, DelayLine&> GetDelayLines();

    void SetHostTempo(float bpm);

    void SetDelayTime(float newDelayTime);
    void SetDelayMode(int newDelayMode);
    void SetFeedbackTime(float newFeedbackTimeSeconds);

    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newQualityStages);

    void SetFiltersOrder(int newOrder);

private:
    float getAmountLower() const;
    float getAmountUpper() const;

    void updateFeedbackGainFromFeedbackTime();
    void updateDynamicDiffusionSizeFromDelayTime();
    void setBlendedStageGains();

    // Parameters
    double sampleRate = 48000.0;
    float hostBPM = 120.0f;

    float feedbackTimeSeconds = 3.0f;
    float feedbackGain = 0.5f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 1.0f;
    int diffusionQualityStages = 8;
    int filtersOrder = 0;

    // Settings
    const float jitterRate = 1.5f;
    const float jitterDepth = 1.5f;

    const float diffusionCompensationBias = 0.5f; // Bigger values = longer swell into nominal
    const float dampingCutoff = 4200.0f;

    // Runtime
    float lastFeedbackL = 0.0f;
    float lastFeedbackR = 0.0f;

    float smoothedBlend = 0.0f;
    float smoothedReadDelayMs = 1.0f;

    float blendSlewCoefficient = 0.0f;
    float readDelaySlewCoefficient = 0.0f;

    float staticCompensationMs = 0.0f;

    DelayTimeSegment delayTimeSegment;

    DelayLine delayLineLeft = DelayLine(0);
    DelayLine delayLineRight = DelayLine(0);

    DeverbDiffusionChain diffusionLeft;
    DeverbDiffusionChain diffusionRight;

    DampingFilter dampingLeft;
    DampingFilter dampingRight;

    Filters* filtersInput = nullptr;
};