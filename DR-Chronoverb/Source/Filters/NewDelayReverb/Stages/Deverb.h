#pragma once

#include <memory>
#include <utility>
#include <algorithm>
#include <cmath>

#include "../DelayLine.h"
#include "../DampingFilter.h"
#include "../DelayTimeSegment.h"
#include "../DeverbDiffusionChain.h"

// Deverb = Delay/Reverb hybrid experiment.
//
// Design:
// 1) input + feedback
// 2) serial allpass diffusion chain BEFORE delay write
// 3) amount-controlled clean/diffused write blend
// 4) amount-controlled read compensation to reveal the swell
// 5) damping after delay read
// 6) feedback unchanged by diffusion amount
//
// This is intentionally separate from the current Delay / Reverb stages
// so it can be auditioned temporarily without disturbing the existing path.
class Deverb
{
public:
    void PrepareToPlay(double newSampleRate);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    std::pair<float, float> ProcessSample(float inputSampleL, float inputSampleR);

    void Reset();

    void SetHostTempo(float bpm);

    void SetDelayTime(float newDelayTime);
    void SetDelayMode(int newDelayMode);
    void SetFeedbackTime(float newFeedbackTimeSeconds);

    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newQualityStages);

private:
    void updateFeedbackGainFromFeedbackTime();

    double sampleRate = 48000.0;
    float hostBPM = 120.0f;

    float feedbackTimeSeconds = 3.0f;
    float feedbackGain = 0.5f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 1.0f;
    int diffusionQualityStages = 8;

    float lastFeedbackL = 0.0f;
    float lastFeedbackR = 0.0f;

    float smoothedBlend = 0.0f;
    float smoothedCompensationMs = 0.0f;
    float smoothedReadDelayMs = 1.0f;

    float blendSlewCoefficient = 0.0f;
    float compensationSlewCoefficient = 0.0f;
    float readDelaySlewCoefficient = 0.0f;

    DelayTimeSegment delayTimeSegment;

    std::unique_ptr<DelayLine> delayLineLeft;
    std::unique_ptr<DelayLine> delayLineRight;

    std::unique_ptr<DampingFilter> dampingLeft;
    std::unique_ptr<DampingFilter> dampingRight;

    DeverbDiffusionChain diffusionLeft;
    DeverbDiffusionChain diffusionRight;
};