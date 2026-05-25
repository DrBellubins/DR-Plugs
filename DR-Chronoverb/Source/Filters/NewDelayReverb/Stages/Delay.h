#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "../DelayLine.h"
#include "../DampingFilter.h"
#include "../DiffusionChain.h"
#include "../../ChronoverbUtils.h"

class DelayLine;
class DiffusionChain;


// Single channel, handles all delay feedback, diffusion, damping, etc.
class Delay
{
public:
    std::vector<float> Tunings =
    {
        10.0, 15.0, 22.0, 33.0, 50.0, 75.0, 113.0, 170.0    // Natural
        //7.0, 13.0, 19.0, 29.0, 53.0, 79.0, 113.0, 149.0   // Generated primes
        //10.0, 20.0, 25.0, 29.0, 53.0, 79.0, 113.0, 149.0  // Primes modified
        //5.0, 11.0, 17.0, 19.0, 23.0, 29.0, 31.0, 37.0     // Bad Deelay approx.
        //5.0, 11.0, 17.0, 23.0, 47.0, 67.0, 71.0, 73.0     // Also bad.
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

    // Settings
    const float centeredSwellRatio = 0.25f;
    const float diffusionCompensationBias = 2.2f; // Controls swell into nominal (higher = longer swell)

    // Runtime
    double sampleRate = 48000.0;
    float hostBpm = 120.0f;

    float lastFeedback = 0.0f;

    int writePeriodSamples = 1;
    int echoWriteCounter = 0;

    int lastBuiltQualityStages = -1;
    float lastBuiltSize01 = -1.0f;

    float totalDelayDiffusionMilliseconds = 0.0f;
    float staticDiffusionCompensationMilliseconds = 0.0f;

    float smoothedCenteredReadDelayMilliseconds = 1.0f;
    float readDelaySlewCoefficient = 0.0f;

    // Parameters
    float delayTimeNormalized = 0.3f;
    float delayMilliseconds = 300.0f;
    int delayMode = 0;

    float feedbackTimeSeconds = 3.0f;
    float feedbackGain = 0.5f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 0.0f;
    int diffusionQualityStages = 8;

    float lowpassCutoff = 0.0f;
    float highpassCutoff = 0.0f;

    // Data
    std::unique_ptr<DelayLine> delayLine;

    std::unique_ptr<DiffusionChain> delayDiffusionRead;
    std::unique_ptr<DiffusionChain> delayDiffusionWrite;

    std::unique_ptr<DampingFilter> damping;

    std::atomic<bool> diffusionRebuildPending { false };
};
