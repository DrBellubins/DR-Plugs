#pragma once

#include <vector>

#include "../Filters.h"
#include "../../DelayTimeSegment.h"
#include "../../DiffusionChain.h"
#include "../../DampingFilter.h"
#include "../../../ChronoverbUtils.h"

// Multi-channel, handles all reverb feedback, diffusion, damping, etc.
class Reverb
{
public:
    std::vector<float> Tunings =
    {
        29.0, 37.0, 43.0, 53.0, 71.0, 89.0, 113.0, 149.0
    };

    void PrepareToPlay(double newSampleRate, Filters& filters);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    std::pair<float, float> ProcessSample(float inputSampleL, float inputSampleR);

    void SetHostTempo(float bpm);

    void SetDelayTime(float newDelayTime);
    void SetDelayMode(int newDelaymode);
    void SetFeedbackTime(float newFeedbackTimeSeconds);

    void SetDiffusionAmount(float newDiffusionAmount);
    void SetDiffusionSize(float newDiffusionSize);
    void SetDiffusionQuality(int newDiffusionQuality);

    void SetFiltersOrder(int newOrder);

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

    int filtersOrder = 0;

    // Data
    DelayTimeSegment delayTimeSegment;

    std::unique_ptr<DiffusionChain> diffusionLeft;
    std::unique_ptr<DiffusionChain> diffusionRight;

    std::unique_ptr<DampingFilter> dampingLeft;
    std::unique_ptr<DampingFilter> dampingRight;

    Filters* filtersInput = nullptr;

    std::atomic<bool> diffusionRebuildPending { false };
};
