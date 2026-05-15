#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>

#include "../Utils/PMath.h"
#include "NewDelayReverb/DelayLine.h"
#include "NewDelayReverb/DampingFilter.h"
#include "NewDelayReverb/DiffusionChain.h"
#include "NewDelayReverb/PitchShifter.h"

class DelayLine;
class DampingFilter;
class DiffusionChain;

class NewDelayReverb
{
public:
    NewDelayReverb();
    ~NewDelayReverb();

    void PrepareToPlay(double sampleRate, float initialHostTempoBpm);
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    // ---------------- Parameter Setters ----------------
    void SetDelayTime(float newDelayTimeNormalized);      // 0..1 -> 0..1000 ms
    void SetFeedbackTime(float newFeedbackTimeSeconds);   // 0..10 s
    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newQualityStages);       // 1..8
    void SetDryWetMix(float newDryWet01);

    void SetLowpassCutoff(float newLowpass01);            // 0..1 -> 500..9000 Hz
    void SetHighpassCutoff(float newHighpass01);          // 0..1 -> 10..2000 Hz
    void SetStereoSpread(float newSpreadMinus1To1);       // -1..1
    void SetHPLPPrePost(float prePost01);                 // 0 = Pre, 1 = Post

    void SetPitchShiftEnabled(float pitchShiftEnabled01);
    void SetPitchShiftRangeLower(float pitchShiftRangeLowerSemitones);
    void SetPitchShiftRangeUpper(float pitchShiftRangeUpperSemitones);
    void SetPitchShiftMode(int modeIndex);                // 0=Up, 1=Down, 2=Random

    void SetHostTempo(float bpm);

private:
    void updateDelayMillisecondsFromNormalized();
    void rebuildDiffusionIfNeeded();
    void updateFeedbackGainFromFeedbackTime();
    void updateFilters();
    void updateStereoSpread();
    void rebuildPitchSequences();

    // Parameters
    double sampleRate    = 48000.0;
    float  hostTempoBpm  = 120.0f;

    float delayTimeNormalized = 0.3f;
    float delayMilliseconds   = 300.0f;

    float feedbackTimeSeconds = 3.0f;
    float feedbackGain        = 0.5f;

    float diffusionAmount01      = 0.0f;
    float diffusionSize01        = 0.0f;
    int   diffusionQualityStages = 6;

    float totalDelayDiffusionMilliseconds          = 0.0f;
    float staticDiffusionCompensationMilliseconds  = 0.0f;

    float smoothedCenteredReadDelayMilliseconds = 1.0f;
    float readDelaySlewCoefficient              = 0.0f;

    float centeredSwellRatio       = 0.25f;
    float diffusionCompensationBias = 1.5f;

    float dryWet01                = 0.5f;
    float lowpass01               = 0.0f;
    float highpass01              = 0.0f;
    float stereoSpreadMinus1To1   = 0.0f;
    float hplpPrePost01           = 1.0f;

    float pitchShiftEnabled    = 0.0f;
    float pitchShiftRangeLower = -12.0f;
    float pitchShiftRangeUpper =  12.0f;
    int   pitchShiftMode       = 0;

    float pitchShifterLatencyMs = 0.0f;

    int echoSampleCounterL = 0;
    int echoSampleCounterR = 0;

    std::atomic<bool> filterRebuildPending    { false };
    std::atomic<bool> diffusionRebuildPending { false };

    // Delay lines
    std::unique_ptr<DelayLine> mainDelayLeft;
    std::unique_ptr<DelayLine> mainDelayRight;

    // Diffusion chains — two pairs:
    //   delayDiffusion  : delay-quality blur (amount 0..0.5 and post-read early tap)
    //   reverbDiffusion : reverb-quality smear (crossfaded in for amount 0.5..1)
    std::unique_ptr<DiffusionChain> delayDiffusionLeft;
    std::unique_ptr<DiffusionChain> delayDiffusionRight;

    std::unique_ptr<DiffusionChain> reverbDiffusionLeft;
    std::unique_ptr<DiffusionChain> reverbDiffusionRight;

    std::unique_ptr<DampingFilter> dampingLeft;
    std::unique_ptr<DampingFilter> dampingRight;

    OctaveEchoPitchShifter wetInputPitchShifterLeft;
    OctaveEchoPitchShifter wetInputPitchShifterRight;

    juce::dsp::IIR::Filter<float> lowpassL;
    juce::dsp::IIR::Filter<float> lowpassR;
    juce::dsp::IIR::Filter<float> highpassL;
    juce::dsp::IIR::Filter<float> highpassR;

    float smoothedDelayReverbDiffBlend = 0.0f;
    float kBlendSlewCoeff              = 0.0f;

    float lastFeedbackL = 0.0f;
    float lastFeedbackR = 0.0f;

    int lastBuiltQualityStages = -1;
    float lastBuiltSize01      = -1.0f;

    static float map01ToRange(float value01, float minValue, float maxValue);
    static float clamp01(float value);
    static int   clampInt(int value, int minValue, int maxValue);
};