#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

#include "../../ChronoverbUtils.h"
#include "../DiffusionChain.h"
#include "../DampingFilter.h"

class DiffusionChain;
class DampingFilter;

class Reverb
{
public:
    std::vector<float> Tunings =
    {
        29.0f, 37.0f, 43.0f, 53.0f, 71.0f, 89.0f, 113.0f, 149.0f
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
    void updateDelayMillisecondsFromNormalized();
    void rebuildDiffusionIfNeeded();
    void updateFeedbackGainFromFeedbackTime();

    // Runtime
    double sampleRate = 48000.0;
    float hostBPM = 120.0f;

    float lastFeedback = 0.0f;
    float feedbackGain = 0.5f;

    int lastBuiltQualityStages = -1;
    float lastBuiltSize01 = -1.0f;

    float smoothedCenteredReadDelayMilliseconds = 1.0f;
    float readDelaySlewCoefficient = 0.0f;

    // Probably not needed
    int writePeriodSamples = 1;
    int echoWriteCounter = 0;

    // Parameters
    float delayTimeNormalized = 0.3f;
    float delayMilliseconds = 300.0f;
    int delayMode = 0;

    float feedbackTimeSeconds = 3.0f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 0.0f;
    int diffusionQualityStages = 8;

    std::unique_ptr<DiffusionChain> diffusion;
    std::unique_ptr<DampingFilter> damping;

    std::atomic<bool> diffusionRebuildPending { false };
};
