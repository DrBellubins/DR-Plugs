#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <cmath>
#include <memory>

// Components
#include "ClusteredDiffusionDelay/DelayLine.h"
#include "ClusteredDiffusionDelay/Diffusion.h"
#include "ClusteredDiffusionDelay/HaasStereoWidener.h"
#include "ClusteredDiffusionDelay/FeedbackDamping.h"
#include "ClusteredDiffusionDelay/Highpass.h"
#include "ClusteredDiffusionDelay/Lowpass.h"
#include "ClusteredDiffusionDelay/Smoothers.h"
#include "ClusteredDiffusionDelay/DryWetMixer.h"
#include "ClusteredDiffusionDelay/Ducking.h"
#include "ClusteredDiffusionDelay/FeedbackDelayNetwork.h"

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
    void SetHostTempo(float HostTempoBPMValue);

    void SetFeedbackTime(float feedbackTimeSeconds);
    void SetDiffusionAmount(float diffusionAmount);
    void SetDiffusionSize(float DiffusionSize);
    void SetDiffusionQuality(int diffusionQuality);
    void SetDryWetMix(float dryWet);

    // - Negative values [-1..0): stereo reducer (mid/side scale). -1 => fully mono.
    // - Zero: no change.
    // - Positive values (0..+1]: Haas widening (delays one channel by up to HaasMaxMs).
    void SetStereoSpread(float stereoWidth);
    void SetHighpassCutoff(float hpFreq);
    void SetLowpassCutoff(float lpFreq);
    void SetHPLPPrePost(float toggle);

    // Ducking
    void SetDuckAmount(float duckAmount);
    void SetDuckAttack(float duckAttack);
    void SetDuckRelease(float duckRelease);

    // Main audio processing (master function). Delegates to component static functions.
    void ProcessBlock(juce::AudioBuffer<float>& AudioBuffer);

private:
    // Convert seconds to samples.
    inline float secondsToSamples(float Seconds) const
    {
        return static_cast<float>(Seconds * static_cast<float>(SampleRate));
    }

    // A small helper to normalize steps -> [0..1].
    inline float stepsToNormalizedQuality(int diffusionQualitySteps) const
    {
        int Clamped = juce::jlimit(0, 10, diffusionQualitySteps);
        return static_cast<float>(Clamped) / 10.0f;
    }

    // Per-channel aggregate state composed of component states.
    struct ChannelState
    {
        DelayLine::State Delay;             // Circular delay line
        HaasStereoWidener::State Haas;      // Haas widener buffer
        FeedbackDamping::State Feedback;    // Feedback damping LPF state

        Highpass::State PreHP;              // Pre-feedback high-pass state
        Lowpass::State PreLP;               // Pre-feedback low-pass state

        Highpass::State PostHP;             // Post-Diffusion high-pass state
        Lowpass::State PostLP;              // Post-Diffusion low-pass state

        Ducking::State Duck;                // Ducking state
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

    // FDN configuration
    const int FDNNumberOfLines = 4;          // 4-line FDN (power of two → Hadamard)
    const bool FDNNormalizeWetMix = true;    // keep WetSum level consistent

    // Jitter LFO for diffusion chain
    float DiffuserJitterPhase = 0.0f;
    float DiffuserJitterPhaseIncrement = 0.0015f; // slow decorrelation across stages

    // Cache of per-line delays (derived from smoothed delay time + spread)
    std::vector<float> FDNLineDelaysSamples;

    // Diffusion chain used to shape the feedback bus before FDN mixing
    Diffusion::AllpassChain DiffusionChain;

    // Feedback Delay Network state
    FeedbackDelayNetwork::State FDNState;

    // Per-channel state container
    std::vector<ChannelState> Channels;

    // Derived for Haas widening
    int HaasMaxDelaySamples = 1;

    // Atomic parameters (targets)
    std::atomic<float> TargetDelayTimeSeconds { 0.300f };
    std::atomic<int> TargetDelayMode{ 0 };                      // 0=ms,1=nrm,2=trip,3=dot
    std::atomic<float> HostTempoBPM { 120.0f };                 // Updated from host

    // Flag to force instantaneous resync of smoothed delay when mode changes
    std::atomic<bool> DelayModeJustChanged { false };

    std::atomic<float> TargetDiffusionAmount  { 0.00f  };
    std::atomic<float> TargetDiffusionSize    { 0.00f  };
    std::atomic<int> TargetDiffusionQuality { 10  };
    std::atomic<float> TargetFeedbackTimeSeconds { 3.00f };
    std::atomic<float> TargetDryWetMix { 1.00f };

    // Filters
    std::atomic<float> TargetStereoWidth { 0.0f };
    std::atomic<float> TargetPreHighpassCuttoff { 0.00f }; // 0..1
    std::atomic<float> TargetPreLowpassCutoff  { 0.00f }; // 0..1
    std::atomic<bool> TargetHPLPPrePost  { true }; // true = pre, false = post

    // Ducking
    std::atomic<float> TargetDuckAmount{ 0.0f };
    std::atomic<float> TargetDuckAttack { 0.0f };
    std::atomic<float> TargetDuckRelease { 0.0f };

    // Safety flag
    bool IsPrepared = false;
};