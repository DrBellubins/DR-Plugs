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

class Deverb
{
public:
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
    void updateFeedbackGainFromFeedbackTime();
    void updateDynamicDiffusionSizeFromDelayTime();

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