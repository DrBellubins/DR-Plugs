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
#include "../../ChronoverbUtils.h"
#include "../../../Utils/PMath.h"

// TODO: Diffusion quality 1 & 2 are broken, make them use the longest allpass delays, with no gain compensation

// TODO: Delay side diffusion is too wide
// TODO: Investigate if jitter-based L/R De-Correlation is better than static tuning De-Correlation

class Deverb
{
public:
    static constexpr float BaseDelayAllpassGain = 0.58f;
    static constexpr float BasedReverbAllpassGain = 1.0f;

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
        2.0, 1.5, 1.0, BaseDelayAllpassGain, BaseDelayAllpassGain, BaseDelayAllpassGain, BaseDelayAllpassGain, BaseDelayAllpassGain
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