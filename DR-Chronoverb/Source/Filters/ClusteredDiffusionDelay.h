#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <cmath>

// External modular processing blocks
#include "AllpassFilter.h"
#include "LowpassFilter.h"
#include "HighpassFilter.h"
#include "StereoWidener.h"

// ClusteredDiffusionDelay (Modularized Version)
// Reimplements the previous integrated delay/diffusion/reverb building block,
// replacing internal bespoke filters and stereo logic with modular classes.
//
// Functional Parity Goals:
//  - Delay time smoothing and diffusion size smoothing preserved.
//  - Prime-based symmetric tap offsets preserved.
//  - Crossfade between base tap and diffused cluster preserved.
//  - Feedback damping behavior recreated using LowpassFilter per channel.
//  - Pre highpass then pre lowpass shaping reproduced with HighpassFilter + LowpassFilter.
//  - Stereo width behavior (negative => mid/side reduction, positive => Haas widening) done
//    through StereoWidener on a temporary wet buffer (exact semantics retained).
//  - Dry/Wet mixing math (equal-power law) preserved.
//
// Added Modular Hooks:
//  - Per-channel filters stored in ChannelState for future extension.
//  - Optional allpass filter placeholder list (not active yet, but included for iterative development).
//
// Usage:
//  - Call PrepareToPlay(sampleRate, maxDelaySeconds) before ProcessBlock.
//  - Update parameters via Set* methods (atomic, real-time safe).
//  - Call ProcessBlock(buffer) each audio block; dry content is mixed in-place.

class ClusteredDiffusionDelay
{
public:
    ClusteredDiffusionDelay();
    ~ClusteredDiffusionDelay();

    // Per-channel delay and filter state
    struct ChannelState
    {
        std::vector<float> DelayBuffer;          // Circular delay line storage
        int WriteIndex = 0;                      // Current write position in delay buffer

        // Modular filters:
        LowpassFilter FeedbackDampingLowpass;    // Replaces previous one-pole feedback damping
        HighpassFilter PreHighpassFilter;        // Pre-feedback highpass stage (decay-shaped)
        LowpassFilter PreLowpassFilter;          // Pre-feedback lowpass stage (decay-shaped)

        // Optional diffusion allpass network (not used yet, reserved for future)
        std::vector<AllpassFilter> DiffusionAllpasses;

        // Initialize filters prepared flag
        bool FiltersPrepared = false;
    };

    // Prepare internal buffers and filter modules
    void PrepareToPlay(double NewSampleRate, float NewMaximumDelaySeconds);

    // Reset all state (clears delay buffers and filter histories)
    void Reset();

    // Parameter setters (atomic). Clamp ranges mirror original implementation.
    void SetDelayTime(float DelayTimeSeconds);
    void SetFeedbackTime(float FeedbackTimeSeconds);
    void SetDiffusionAmount(float DiffusionAmount);
    void SetDiffusionSize(float DiffusionSize);
    void SetDiffusionQuality(float DiffusionQuality);
    void SetDryWetMix(float DryWetMix);

    void SetStereoSpread(float StereoWidth);
    void SetLowpassDecay(float DecayAmount);
    void SetHighpassDecay(float DecayAmount);

    // Main audio processing (in-place). Applies diffusion + feedback + stereo width + dry/wet.
    void ProcessBlock(juce::AudioBuffer<float>& AudioBuffer);

private:
    // Ensure enough channel states exist and (re)prepare filters for new channels
    void ensureChannelState(int RequiredChannels);

    // Recompute symmetric tap offset layout from diffusion quality
    void recomputeTargetTapLayout();

    // Update smoothing priming values per block (delay time & diffusion size)
    void updateBlockSmoothing(int NumSamples);

    // Convert fractional seconds to samples
    inline float secondsToSamples(float Seconds) const
    {
        return static_cast<float>(Seconds * static_cast<float>(SampleRate));
    }

    // Read with fractional delay (linear interpolation) from delay buffer
    static inline float readFromDelayBuffer(const ChannelState& State, float DelayInSamples);

    // Write a sample to the circular delay buffer
    static inline void writeToDelayBuffer(ChannelState& State, float SampleValue);

    // One-pole smoothing helper
    static inline float smoothOnePole(float CurrentValue, float TargetValue, float Coefficient);

    // Map diffusion quality [0..1] → number of symmetric tap pairs [1..8]
    int qualityToTapPairs(float Quality) const;

    // Compute damping cutoff mapping replicated from previous logic, returning Hz
    float computeDampingCutoffHz() const;

    // Compute equal-power crossfade coefficients for diffusion amount
    void computeDiffusionCrossfade(float DiffusionAmount, float& OutAmountA, float& OutAmountB) const;

    // Map T60 decay to feedback gain given loop period seconds
    static float t60ToFeedbackGain(float LoopSeconds, float T60Seconds);

    // Map decay slider amounts to actual HP / LP cutoffs (same curves as before)
    void mapDecayAmountsToCutoffs(float LowpassDecayAmount,
                                  float HighpassDecayAmount,
                                  float& OutLowpassCutoffHz,
                                  float& OutHighpassCutoffHz) const;

    // Reconfigure pre filters' cutoffs (called once per block)
    void updatePreFilterCutoffs();

    // Reconfigure feedback damping lowpass cutoff (once per block)
    void updateFeedbackDampingCutoff();

    // Sample rate and buffer sizing
    double SampleRate = 44100.0;
    int MaxDelayBufferSamples = 1;
    float MaximumDelaySeconds = 1.0f;

    // Maximum spread window (derived from MaximumDelaySeconds, capped)
    float MaximumSpreadSeconds = 0.100f;

    // Atomic target parameters (thread-safe)
    std::atomic<float> TargetDelayTimeSeconds { 0.300f };
    std::atomic<float> TargetDiffusionAmount  { 0.00f  };
    std::atomic<float> TargetDiffusionSize    { 0.00f  };
    std::atomic<float> TargetDiffusionQuality { 1.00f  };
    std::atomic<float> TargetFeedbackTimeSeconds { 3.00f };
    std::atomic<float> TargetDryWetMix { 1.00f };

    std::atomic<float> TargetStereoWidth { 0.0f };
    std::atomic<float> TargetPreLowpassDecayAmount  { 0.00f };
    std::atomic<float> TargetPreHighpassDecayAmount { 0.00f };

    // Smoothed live values
    float SmoothedDelayTimeSeconds = 0.300f;
    float SmoothedDiffusionSize    = 0.00f;

    // Smoothing coefficients
    float DelayTimeSmoothCoefficient = 0.0015f;
    float SizeSmoothCoefficient      = 0.0020f;

    // Symmetric tap offsets (normalized)
    std::vector<float> NormalizedSymmetricOffsets;
    const int PrimeLikeSequence[8] = { 2, 3, 5, 7, 11, 13, 17, 19 };

    // Per-channel state list
    std::vector<ChannelState> Channels;

    // Stereo widener instance (modular replacement for embedded Haas/mid-side)
    StereoWidener StereoWidthProcessor;

    // Scratch wet buffer reused per block to apply stereo width processing
    juce::AudioBuffer<float> WetScratchBuffer;

    // Flag after successful preparation
    bool IsPrepared = false;
};