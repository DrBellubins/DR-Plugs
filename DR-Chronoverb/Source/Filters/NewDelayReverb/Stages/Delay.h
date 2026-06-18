#pragma once

#include <atomic>
#include <memory>
#include <vector>

#include "Filters.h"
#include "../DelayLine.h"
#include "../DampingFilter.h"
#include "../DiffusionChain.h"
#include "../DelayTimeSegment.h"
#include "../../ChronoverbUtils.h"

// TODO: Delay time doesn't seem to affect the diffusion decay time
// TODO: Use Lissajous Stereo Rotation instead of static tuning decorrelation.

// Single-channel, handles all delay feedback, diffusion, damping, etc.
class Delay
{
public:
    std::vector<float> Tunings =
    {
        11.0f, 13.0f, 23.0f, 31.0f, 43.0f, 53.0f, 73.0f, 83.0f // Whatever
        //5.0, 13.0, 19.0, 29.0, 31.0, 47.0, 73.0, 89.0       // Snappy primes
        //10.0, 15.0, 22.0, 33.0, 50.0, 75.0, 113.0, 170.0  // Natural
        //7.0, 13.0, 19.0, 29.0, 53.0, 79.0, 113.0, 149.0   // Generated primes
        //10.0, 20.0, 25.0, 29.0, 53.0, 79.0, 113.0, 149.0  // Primes modified
        //5.0, 11.0, 17.0, 19.0, 23.0, 29.0, 31.0, 37.0     // Bad Deelay approx.
        //5.0, 11.0, 17.0, 23.0, 47.0, 67.0, 71.0, 73.0     // Also bad.
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
    const float tuningLengthMultiplier = 0.25f;
    const float MinimumBPM = 20.0f; // Silently breaks below this point
    const float centeredSwellRatio = 0.25f;
    const float diffusionCompensationBias = 5.0f; // Controls swell into nominal (higher = longer swell)

    // Runtime
    double sampleRate = 48000.0f;
    float hostBPM = 120.0f;

    float maxDelayMS = 0.0f;

    float lastFeedbackL = 0.0f;
    float lastFeedbackR = 0.0f;

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

    int filtersOrder = 0;

    // Data
    DelayTimeSegment delayTimeSegment;

    std::unique_ptr<DelayLine> delayLineLeft;
    std::unique_ptr<DelayLine> delayLineRight;

    std::unique_ptr<DiffusionChain> diffusionReadLeft;
    std::unique_ptr<DiffusionChain> diffusionWriteLeft;

    std::unique_ptr<DiffusionChain> diffusionReadRight;
    std::unique_ptr<DiffusionChain> diffusionWriteRight;

    std::unique_ptr<DampingFilter> dampingLeft;
    std::unique_ptr<DampingFilter> dampingRight;

    Filters* filtersInput = nullptr;

    std::atomic<bool> diffusionRebuildPending { false };
};
