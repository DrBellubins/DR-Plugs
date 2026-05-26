#pragma once

#include <vector>

#include "../DelayTimeSegment.h"
#include "../DiffusionChain.h"
#include "../DampingFilter.h"

class Reverb
{
public:
    std::vector<float> Tunings =
    {
        29.0, 37.0, 43.0, 53.0, 71.0, 89.0, 113.0, 149.0
    };

    void PrepareToPlay(double newSampleRate);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    float ProcessSample(float inputSample);

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
    const float irLengthMs = 2000.0f;

    // Runtime
    double sampleRate = 48000.0;
    float hostBPM = 120.0f;

    float lastFeedback = 0.0f;
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

    std::unique_ptr<DiffusionChain> diffusion;
    std::unique_ptr<DampingFilter> damping;

    std::atomic<bool> diffusionRebuildPending { false };
};
