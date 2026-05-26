#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "../DelayLine.h"
#include "../DampingFilter.h"
#include "../DiffusionChain.h"
#include "../DelayTimeSegment.h"

// TODO: Wet signal has white noise (DC artifacts??)
// TODO: Sounds too mono when compared to Deelay?

// Single channel, handles all delay feedback, diffusion, damping, etc.
class Delay
{
public:
    // TODO: Need to be scaled by 0.25 to fit Deelay
    std::vector<float> Tunings =
    {
        5.0, 13.0, 19.0, 29.0, 31.0, 47.0, 73.0, 89.0       // Snappy primes
        //10.0, 15.0, 22.0, 33.0, 50.0, 75.0, 113.0, 170.0  // Natural
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
    void rebuildDiffusionIfNeeded();
    void updateFeedbackGainFromFeedbackTime();

    // Settings
    const float tuningLengthMultiplier = 0.25f;
    const float MinimumBPM = 20.0f; // Silently breaks below this point
    const float centeredSwellRatio = 0.25f;
    const float diffusionCompensationBias = 5.0f; // Controls swell into nominal (higher = longer swell)

    // Runtime
    double sampleRate = 48000.0f;
    float hostBPM = 120.0f;

    float maxDelayMS = 0.0f;

    float lastFeedback = 0.0f;
    float feedbackGain = 0.5f;

    int writePeriodSamples = 1;
    int echoWriteCounter = 0;

    int lastBuiltQualityStages = -1;
    float lastBuiltSize = -1.0f;

    float totalDelayDiffusionMilliseconds = 0.0f;
    float staticDiffusionCompensationMilliseconds = 0.0f;

    float smoothedCenteredReadDelayMilliseconds = 1.0f;
    float readDelaySlewCoefficient = 0.0f;

    // Parameters
    float feedbackTimeSeconds = 3.0f;

    float diffusionAmount = 0.0f;
    float diffusionSize = 0.0f;
    int diffusionQualityStages = 8;

    //float lowpassCutoff = 0.0f;
    //float highpassCutoff = 0.0f;

    // Data
    DelayTimeSegment delayTimeSegment;

    std::unique_ptr<DelayLine> delayLine;

    std::unique_ptr<DiffusionChain> diffusionRead;
    std::unique_ptr<DiffusionChain> diffusionWrite;

    std::unique_ptr<DampingFilter> damping;

    std::atomic<bool> diffusionRebuildPending { false };
};
