#pragma once

#include <vector>

#include "../DelayTimeSegment.h"
#include "../DiffusionChain.h"
#include "../DampingFilter.h"

// TODO: Wet signal has white noise (DC artifacts??)

class Reverb
{
public:
    std::vector<float> TuningsLeft =
    {
        29.0, 37.0, 43.0, 53.0, 71.0, 89.0, 113.0, 149.0
    };

    std::vector<float> TuningsRight =
    {
        31.0, 41.0, 47.0, 59.0, 73.0, 97.0, 109.0, 151.0
    };

    void PrepareToPlay(double newSampleRate);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    std::pair<float, float> ProcessSample(float inputSampleL, float inputSampleR);

    void SetHostTempo(float bpm);

    void SetDelayTime(float newDelayTime);
    void SetDelayMode(int newDelaymode);
    void SetFeedbackTime(float newFeedbackTimeSeconds);

    void SetDiffusionAmount(float newDiffusionAmount);
    void SetDiffusionSize(float newDiffusionSize);
    void SetDiffusionQuality(int newDiffusionQuality);

private:
    void rebuildDiffusionIfNeeded();
    void updateFeedbackGainFromFeedbackTime();

    // Settings
    const float tuningLengthMultiplier = 2.0f;
    const float irLengthMs = 1600.0f;

    // Runtime
    double sampleRate = 48000.0;
    float hostBPM = 120.0f;

    float lastFeedbackL = 0.0f;
    float lastFeedbackR = 0.0f;

    float feedbackGain = 0.5f;

    int lastBuiltQualityStages = -1;
    float lastBuiltSize = -1.0f;

    float smoothedCenteredReadDelayMilliseconds = 1.0f;
    float readDelaySlewCoefficient = 0.0f;

    // Parameters
    float feedbackTimeSeconds = 3.0f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 0.0f;
    int diffusionQualityStages = 8;

    // Data
    DelayTimeSegment delayTimeSegment;

    std::unique_ptr<DiffusionChain> diffusionLeft;
    std::unique_ptr<DiffusionChain> diffusionRight;

    std::unique_ptr<DampingFilter> dampingLeft;
    std::unique_ptr<DampingFilter> dampingRight;

    std::atomic<bool> diffusionRebuildPending { false };
};
