#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>

#include "NewDelayReverb/DelayLine.h"
#include "NewDelayReverb/DampingFilter.h"
#include "NewDelayReverb/DiffusionChain.h"
#include "NewDelayReverb/FeedbackDelayNetwork.h"

// Forward declarations of internal components
class DelayLine;
class DampingFilter;
class DiffusionChain;
class SimpleFDN;

// NewDelayReverb
// A minimal, modular delay+reverb core based on the Deelay estimate:
// - Input + feedback sum
// - Diffusion (chain of allpass filters; amount, size, quality)
// - Main delay line (fixed max 1000 ms buffer, adjustable read offset via delayTime 0..1 -> 0..1000 ms)
// - Damping lowpass in feedback path
// - Feedback gain
// - Dry/Wet mix
//
// This class is intentionally self-contained and uses simple parameters in normalized ranges.
// Later, you can wire these to AudioProcessorValueTreeState parameters in your existing processor.
class NewDelayReverb
{
public:
    NewDelayReverb();
    ~NewDelayReverb();

    // Prepare DSP for given sample rate and block size
    void PrepareToPlay(double sampleRate, float initialHostTempoBpm);

    // Process a single audio buffer in-place (stereo supported, mono also works)
    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);

    // ---------------- Parameter Setters ----------------
    // All normalized except feedbackGain and tempo.
    // delayTime: 0..1 mapped to 0..1000 ms
    void SetDelayTime(float newDelayTimeNormalized);
    // feedbackTime: conceptual time; we map it to feedbackGain via a simple curve for now.
    // For basic functionality, we treat this as a gain shaper: larger -> more feedback.
    void SetFeedbackTime(float newFeedbackTimeSeconds);
    void SetDiffusionAmount(float newAmount01);
    void SetDiffusionSize(float newSize01);
    void SetDiffusionQuality(int newQualityStages);  // 0..10 -> number of allpass stages
    void SetDryWetMix(float newDryWet01);

    // Filters and spread
    void SetLowpassCutoff(float newLowpass01);  // 0..1 mapped to [500 Hz .. 9000 Hz]
    void SetHighpassCutoff(float newHighpass01); // 0..1 mapped to [10 Hz .. 2000 Hz] (basic HP to tame DC/rumble)
    void SetStereoSpread(float newSpreadMinus1To1); // -1..1 basic widening/narrowing
    void SetHPLPPrePost(float prePost01); // 0 => Pre, 1 => Post

    // Host tempo for sync modes (not used deeply yet, but retained for future porting)
    void SetHostTempo(float bpm);

private:
    // Internal helpers
    void updateDelayMillisecondsFromNormalized();
    void rebuildDiffusionIfNeeded();
    void updateFeedbackGainFromFeedbackTime();
    void updateFilters();
    void updateStereoSpread();

    // Compensation for diffusion group delay (milliseconds)
    float diffusionGroupDelayMilliseconds = 0.0f;

    // Parameters
    double sampleRate = 48000.0;
    float hostTempoBpm = 120.0f;

    float delayTimeNormalized = 0.3f;  // 300 ms
    float delayMilliseconds = 300.0f;

    float feedbackTimeSeconds = 3.0f;
    float feedbackGain = 0.5f;         // core feedback gain

    float diffusionAmount01 = 0.0f;    // crossfade amount
    float diffusionSize01 = 0.0f;      // scales individual allpass delay lengths
    int diffusionQualityStages = 6;    // number of allpass stages; will clamp 4..8

    float dryWet01 = 0.5f;

    float lowpass01 = 0.0f;
    float highpass01 = 0.0f;
    float stereoSpreadMinus1To1 = 0.0f;
    float hplpPrePost01 = 1.0f; // default Post

    // Components
    std::unique_ptr<DelayLine> mainDelayLeft;
    std::unique_ptr<DelayLine> mainDelayRight;

    std::unique_ptr<DiffusionChain> diffusionLeft;
    std::unique_ptr<DiffusionChain> diffusionRight;

    std::unique_ptr<DampingFilter> dampingLeft;
    std::unique_ptr<DampingFilter> dampingRight;

    // Simple FDN placeholder for future extension (currently unused in core path,
    // but provided to match requested modular breakdown)
    std::unique_ptr<SimpleFDN> fdnLeft;
    std::unique_ptr<SimpleFDN> fdnRight;

    // Basic HP/LP filters (JUCE one-pole IIR) for pre/post spectral shaping
    juce::dsp::IIR::Filter<float> lowpassL;
    juce::dsp::IIR::Filter<float> lowpassR;
    juce::dsp::IIR::Filter<float> highpassL;
    juce::dsp::IIR::Filter<float> highpassR;

    // Temporary per-sample state
    float lastFeedbackL = 0.0f;
    float lastFeedbackR = 0.0f;

    // Utility
    static float map01ToRange(float value01, float minValue, float maxValue);
    static float clamp01(float value);
    static int clampInt(int value, int minValue, int maxValue);
};