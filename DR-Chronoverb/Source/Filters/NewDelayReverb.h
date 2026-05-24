#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>

#include "../Utils/PMath.h"
#include "NewDelayReverb/DelayLine.h"
#include "NewDelayReverb/DampingFilter.h"
#include "NewDelayReverb/DiffusionChain.h"
#include "NewDelayReverb/PitchShifter.h"
#include "NewDelayReverb/DiffusionAllpass.h"

class DelayLine;
class DampingFilter;
class DiffusionChain;

class NewDelayReverb
{
public:
    // Delay-mode tunings: shorter, natural-spacing delays for discrete-tap blur.
    std::vector<float> DelayTunings =
    {
        10.0, 15.0, 22.0, 33.0, 50.0, 75.0, 113.0, 170.0    // Natural
        //7.0, 13.0, 19.0, 29.0, 53.0, 79.0, 113.0, 149.0   // Generated primes
        //10.0, 20.0, 25.0, 29.0, 53.0, 79.0, 113.0, 149.0  // Primes modified
        //5.0, 11.0, 17.0, 19.0, 23.0, 29.0, 31.0, 37.0     // Bad Deelay approx.
        //5.0, 11.0, 17.0, 23.0, 47.0, 67.0, 71.0, 73.0     // Also bad.
    };

    // Reverb-mode tunings: longer, prime-spaced delays for lush modal density.
    std::vector<float> ReverbTunings =
    {
        29.0f, 37.0f, 43.0f, 53.0f, 71.0f, 89.0f, 113.0f, 149.0f
    };

    std::vector<float> PitchMaskTunings
    {
        5.0f, 8.0f, 12.0f, 17.0f, 23.0f, 31.0f, 43.0f, 59.0f
    };

    NewDelayReverb();
    ~NewDelayReverb();

    void PrepareToPlay(double sampleRate, float initialHostTempoBpm);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    // ---------------- Parameter Setters ----------------
    void SetHostTempo(float bpm);
    void SetDelayTime(float newDelayTimeNormalized);      // 0..1 -> 0..1000 ms
    void SetDelayMode(int newDelayMode);

    void SetFeedbackTime(float newFeedbackTimeSeconds);   // 0..10 s
    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newQualityStages);       // 1..8

    void SetDryVolume(float newDryVolume);
    void SetWetVolume(float newWetVolume);

    void SetLowpassCutoff(float newLowpass01);            // 0..1 -> 500..9000 Hz
    void SetHighpassCutoff(float newHighpass01);          // 0..1 -> 10..2000 Hz
    void SetStereoSpread(float newSpreadMinus1To1);       // -1..1
    void SetHPLPPrePost(float prePost01);                 // 0 = Pre, 1 = Post

    void SetPitchRangeLower(float pitchRangeLowerSemitones);
    void SetPitchRangeUpper(float pitchRangeUpperSemitones);
    void SetPitchSequence(int sequenceIndex);                // 0=Up, 1=Down, 2=Random
    void SetPitchStereoEnabled(float enabled01);
    void SetpitchWetMix(float wetVolume);

private:
    void updateDelayMillisecondsFromNormalized();
    void rebuildDiffusionIfNeeded();
    void updateFeedbackGainFromFeedbackTime();
    void updateFilters() const;
    void rebuildPitchSequences();

    static int semitonesToOctaveIndex(float semitones);

    static float map01ToRange(float value01, float minValue, float maxValue);
    static float clamp01(float value);
    static int clampInt(int value, int minValue, int maxValue);

    // Runtime Values
    float lastFeedbackL = 0.0f;
    float lastFeedbackR = 0.0f;

    int echoSampleCounterL = 0;
    int echoSampleCounterR = 0;

    int writePeriodSamples = 1;
    int echoWriteCounterL = 0;
    int echoWriteCounterR = 0;

    float smoothedDelayReverbDiffBlend = 0.0f;
    float kBlendSlewCoeff = 0.0f;

    int lastBuiltQualityStages = -1;
    float lastBuiltSize01 = -1.0f;

    float totalDelayDiffusionMilliseconds = 0.0f;
    float staticDiffusionCompensationMilliseconds = 0.0f;

    float smoothedCenteredReadDelayMilliseconds = 1.0f;
    float readDelaySlewCoefficient = 0.0f;

    float lastPitchDiffFeedbackL = 0.0f;
    float lastPitchDiffFeedbackR = 0.0f;
    float pitchDiffFeedbackGain = 0.6f;

    // Settings
    const float centeredSwellRatio = 0.25f;
    const float diffusionCompensationBias = 2.2f; // Controls swell into nominal (higher = longer swell)

    const float pitchAllpassTuningMultiplier = 1.5f; // For secondary allpass filter tuning
    const float pitchDelayAllpassTuning = 170.0f;
    const float pitchReverbAllpassTuning = 50.0f;

    // Latency
    float cachedPitchCompensationMs = 0.0f;
    float pitchShifterLatencyMs = 0.0f;

    // Parameters
    double sampleRate = 48000.0;
    float hostTempoBpm = 120.0f;

    float delayTimeNormalized = 0.3f;
    float delayMilliseconds = 300.0f;
    int delayMode = 0;

    float feedbackTimeSeconds = 3.0f;
    float feedbackGain = 0.5f;

    float diffusionAmount01 = 0.0f;
    float diffusionSize01 = 0.0f;
    int diffusionQualityStages = 6;

    float dryVolume = 1.0f;
    float wetVolume = 1.0f;

    float lowpass01 = 0.0f;
    float highpass01 = 0.0f;
    float stereoSpreadMinus1To1 = 0.0f;
    float hplpPrePost01 = 1.0f;

    float pitchRangeLower = -12.0f;
    float pitchRangeUpper = 12.0f;
    int pitchMode = 0;
    float pitchStereoEnabled01 = 0.0f;
    float pitchWetMix = 0.0f;

    std::atomic<bool> filterRebuildPending { false };
    std::atomic<bool> diffusionRebuildPending { false };
    std::atomic<bool> pitchSequenceRebuildPending { false };

    // Delay lines
    std::unique_ptr<DelayLine> delayLineLeft;
    std::unique_ptr<DelayLine> delayLineRight;

    // Diffusion chains — two pairs:
    //   delayDiffusion  : delay-quality blur (amount 0..0.5 and post-read early tap)
    //   reverbDiffusion : reverb-quality smear (crossfaded in for amount 0.5..1)
    std::unique_ptr<DiffusionChain> delayDiffusionReadLeft;
    std::unique_ptr<DiffusionChain> delayDiffusionReadRight;

    std::unique_ptr<DiffusionChain> delayDiffusionWriteLeft;
    std::unique_ptr<DiffusionChain> delayDiffusionWriteRight;

    std::unique_ptr<DiffusionChain> reverbDiffusionLeft;
    std::unique_ptr<DiffusionChain> reverbDiffusionRight;

    std::unique_ptr<DampingFilter> dampingLeft;
    std::unique_ptr<DampingFilter> dampingRight;

    // Pitch shifting
    OctaveEchoPitchShifter pitchShifterLeft;
    OctaveEchoPitchShifter pitchShifterRight;

    // Pitch shifting diffusion
    std::unique_ptr<DiffusionChain> pitchDiffusionLeft;
    std::unique_ptr<DiffusionChain> pitchDiffusionRight;

    juce::dsp::IIR::Filter<float> lowpassL;
    juce::dsp::IIR::Filter<float> lowpassR;
    juce::dsp::IIR::Filter<float> highpassL;
    juce::dsp::IIR::Filter<float> highpassR;
};