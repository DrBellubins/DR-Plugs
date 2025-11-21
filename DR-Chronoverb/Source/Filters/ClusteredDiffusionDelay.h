#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <cmath>

// Components
#include "ClusteredDiffusionDelay/DelayLine.h"
#include "ClusteredDiffusionDelay/Diffusion.h"
#include "ClusteredDiffusionDelay/HaasStereoWidener.h"
#include "ClusteredDiffusionDelay/FeedbackDamping.h"
#include "ClusteredDiffusionDelay/Highpass.h"
#include "ClusteredDiffusionDelay/Lowpass.h"
#include "ClusteredDiffusionDelay/Smoothers.h"
#include "ClusteredDiffusionDelay/DryWetMixer.h"

// ClusteredDiffusionDelay (Master)
// - Orchestrates modular components to realize the diffused delay/reverb algorithm.
// - Holds parameters (as atomics) and minimal state; delegates actual processing to static component classes.
// - Maintains per-channel sub-states for delay line, feedback dampers, and pre-filters.
// - Strongly commented to clarify signal flow and component responsibilities.

class ClusteredDiffusionDelay
{
public:
    ClusteredDiffusionDelay();
    ~ClusteredDiffusionDelay();

    // Prepare delay lines and internal state.
    // MaximumDelaySeconds defines the headroom of the circular buffer (nominal delay + spread + safety).
    void PrepareToPlay(double NewSampleRate, float NewMaximumDelaySeconds);

    // Reset delay lines and filters to a neutral state (clears buffers and states).
    void Reset();

    // Parameter setters (thread-safe, real-time safe).
    // Ranges:
    // - DelayTimeSeconds:   [0.0, MaximumDelaySeconds] (clamped internally)
    // - FeedbackTimeSeconds:[0.0, 10.0]  (T60 target in seconds; 0 disables feedback)
    // - DiffusionAmount:    [0.0, 1.0]
    // - DiffusionSize:      [0.0, 1.0]
    // - DiffusionQuality:   [0.0, 1.0]
    void SetDelayTime(float delayTimeSeconds);
    void SetDelayMode(int modeIndex);

    void SetFeedbackTime(float feedbackTimeSeconds);
    void SetDiffusionAmount(float diffusionAmount);
    void SetDiffusionSize(float DiffusionSize);
    void SetDiffusionQuality(float diffusionQuality);
    void SetDryWetMix(float dryWet);

    // - Negative values [-1..0): stereo reducer (mid/side scale). -1 => fully mono.
    // - Zero: no change.
    // - Positive values (0..+1]: Haas widening (delays one channel by up to HaasMaxMs).
    void SetStereoSpread(float stereoWidth);
    void SetHighpassCutoff(float hpFreq);
    void SetLowpassCutoff(float lpFreq);
    void SetHPLPPrePost(float toggle);

    // Main audio processing (master function). Delegates to component static functions.
    void ProcessBlock(juce::AudioBuffer<float>& AudioBuffer);

private:
    // Convert seconds to samples.
    inline float secondsToSamples(float Seconds) const
    {
        return static_cast<float>(Seconds * static_cast<float>(SampleRate));
    }

    // Per-channel aggregate state composed of component states.
    struct ChannelState
    {
        DelayLine::State Delay;              // Circular delay line
        HaasStereoWidener::State Haas;       // Haas widener buffer
        FeedbackDamping::State Feedback;     // Feedback damping LPF state
        Highpass::State PreHP;            // Pre-feedback high-pass state
        Lowpass::State PreLP;             // Pre-feedback low-pass state
    };

    // Sample rate and buffer sizing
    double SampleRate = 44100.0;
    int MaxDelayBufferSamples = 1;            // Allocated per channel
    float MaximumDelaySeconds = 1.0f;         // Provided in PrepareToPlay

    // Maximum cluster spread window derived from MaximumDelaySeconds (capped).
    float MaximumSpreadSeconds = 0.100f;      // 100 ms by default

    // Smoothing state for time-varying parameters
    float SmoothedDelayTimeSeconds = 0.300f;
    float SmoothedDiffusionSize = 0.00f;

    // Smoothing coefficients (tuned for responsiveness vs. stability)
    float DelayTimeSmoothCoefficient = 0.0015f;
    float SizeSmoothCoefficient = 0.0020f;

    // Tap layout for diffusion cluster (recomputed when quality changes).
    Diffusion::TapLayout TapLayout;

    // Per-channel state container
    std::vector<ChannelState> Channels;

    // Derived for Haas widening
    int HaasMaxDelaySamples = 1;

    // Atomic parameters (targets)
    std::atomic<float> TargetDelayTimeSeconds { 0.300f };
    std::atomic<int> TargetDelayMode{ 0 };

    std::atomic<float> TargetDiffusionAmount  { 0.00f  };
    std::atomic<float> TargetDiffusionSize    { 0.00f  };
    std::atomic<float> TargetDiffusionQuality { 1.00f  };
    std::atomic<float> TargetFeedbackTimeSeconds { 3.00f };
    std::atomic<float> TargetDryWetMix { 1.00f };

    std::atomic<float> TargetStereoWidth { 0.0f };
    std::atomic<float> TargetPreHighpassDecayAmount { 0.00f }; // 0..1
    std::atomic<float> TargetPreLowpassDecayAmount  { 0.00f }; // 0..1
    std::atomic<bool> TargetHPLPPrePost  { true }; // true = pre, false = post

    // Safety flag
    bool IsPrepared = false;
};