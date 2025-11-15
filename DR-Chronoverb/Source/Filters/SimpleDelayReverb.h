#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <cmath>

// SimpleDelayReverb
// A compact delay/reverb building block that generates a diffused echo cluster around a nominal delay.
// Key ideas implemented per your algorithm description:
//
// 1) Input Signal Acquisition and Initial Delay
//    - The incoming signal is written to a per-channel circular delay buffer sized by PrepareToPlay.
//    - A nominal delay time (in seconds) determines the base echo time.
//    - NEW: A "base tap" at the nominal delay is always available.
//
// 2) Diffusion Parameter Scaling
//    - DiffusionAmount: Crossfades between the base tap (0) and the diffused cluster (1).
//    - DiffusionSize: Temporal spread of the cluster (small: tight cluster, large: spacious).
//    - DiffusionQuality: Density granularity (low: few sparse taps, high: many smooth taps).
//
// 3) Symmetric Cluster Generation
//    - Each nominal echo is replaced by a set of symmetric taps around the center (± offsets).
//    - "Negative" offsets are implemented causally by shifting the reference with a fixed look-ahead
//      equal to half of the maximum spread (no future peeking required).
//    - Offsets are deterministic and pseudo prime-based to avoid ringing, scaled by DiffusionSize.
//
// 4) Density Buildup via Feedback
//    - The recirculated signal crossfades between base tap (pure delay repeats) and diffused cluster (reverberant tail).
//    - A simple one-pole low-pass damping filter in the feedback path shapes a natural decay.
//    - Feedback gain is mapped from a user T60 ("feedbackTime"), so pure delay repeats exist at Amount = 0.
//
// 5) Pitch Modulation Handling
//    - Real-time changes to delay time and spread are smoothed with one-pole lag processors.
//    - This reduces zipper noise and softens pitch glides that occur when delay times move.
//
// Usage notes:
// - Call PrepareToPlay before ProcessBlock.
// - Set parameters at any time from the audio thread via the Set* methods (they are atomic).
// - ProcessBlock adds the wet signal on top of the existing buffer content (dry path remains intact).
//   If you need a dry/wet control, do it outside this class or extend this class accordingly.
// - External parameter ownership should be done with AudioProcessorValueTreeState in your processor.

class SimpleDelayReverb
{
public:
    SimpleDelayReverb();
    ~SimpleDelayReverb();

    // Prepare delay lines and internal state.
    // MaximumDelaySeconds defines the headroom of the circular buffer (nominal delay + spread + safety).
    void PrepareToPlay(double SampleRate, float MaximumDelaySeconds);

    // Reset delay lines and filters to a neutral state (clears buffers and states).
    void Reset();

    // Parameter setters (thread-safe, real-time safe).
    // Ranges:
    // - DelayTimeSeconds:   [0.0, MaximumDelaySeconds] (clamped internally)
    // - DiffusionAmount:    [0.0, 1.0]
    // - DiffusionSize:      [0.0, 1.0]
    // - DiffusionQuality:   [0.0, 1.0]
    // - FeedbackTimeSeconds:[0.0, 10.0]  (T60 target in seconds; 0 disables feedback)
    void SetDelayTime(float DelayTimeSeconds);
    void SetDiffusionAmount(float DiffusionAmount);
    void SetDiffusionSize(float DiffusionSize);
    void SetDiffusionQuality(float DiffusionQuality);
    void SetFeedbackTime(float FeedbackTimeSeconds);

    // Main audio processing. Adds wet signal on top of input buffer content (in-place).
    void ProcessBlock(juce::AudioBuffer<float>& AudioBuffer);

private:
    // Per-channel delay state
    struct ChannelState
    {
        std::vector<float> DelayBuffer;  // Circular delay line
        int WriteIndex = 0;              // Write pointer
        float FeedbackState = 0.0f;      // Feedback low-pass filter state
    };

    // Internal helpers
    void ensureChannelState(int RequiredChannels);
    void resizeDelayBuffers();
    void recomputeTargetTapLayout();
    void updateBlockSmoothing(int NumSamples);

    // Delay read with linear interpolation from circular buffer (fractional delays).
    inline float readFromDelayBuffer(const ChannelState& State, float DelayInSamples) const;

    // Write a sample into the circular buffer and advance the write pointer.
    inline void writeToDelayBuffer(ChannelState& State, float Sample);

    // One-pole smoothing helper (for time-varying parameters).
    inline float smoothOnePole(float Current, float Target, float Coefficient) const;

    // Map quality to number of symmetric tap pairs (density).
    int qualityToTapPairs(float Quality) const;

    // Compute damping coefficient for the feedback low-pass based on amount/quality.
    float computeDampingCoefficient(float SampleRate) const;

    // Convert seconds to samples (safely).
    inline float secondsToSamples(float Seconds) const
    {
        return static_cast<float>(Seconds * static_cast<float>(SampleRate));
    }

    // Map T60 decay time to per-loop feedback gain given the current loop period (nominal delay).
    // g = 10^(-3 * LoopSeconds / T60Seconds). T60Seconds == 0 => 0.
    float t60ToFeedbackGain(float LoopSeconds, float T60Seconds) const;

    // Sample rate and buffer sizing
    double SampleRate = 44100.0;
    int MaxDelayBufferSamples = 1;            // Allocated per channel
    float MaximumDelaySeconds = 1.0f;         // Provided in PrepareToPlay

    // We allow a maximum spread window that scales with MaximumDelaySeconds (capped).
    // The "look-ahead shift" will be half of this window (in samples) to allow symmetric negative offsets.
    float MaximumSpreadSeconds = 0.100f;      // 100 ms by default (can be derived dynamically)

    // Atomic parameters (targets)
    std::atomic<float> TargetDelayTimeSeconds { 0.300f };
    std::atomic<float> TargetDiffusionAmount  { 0.00f  };
    std::atomic<float> TargetDiffusionSize    { 0.00f  };
    std::atomic<float> TargetDiffusionQuality { 1.00f  };
    std::atomic<float> TargetFeedbackTimeSeconds { 3.00f };

    // Smoothed parameters used inside the tight loop
    float SmoothedDelayTimeSeconds = 0.300f;
    float SmoothedDiffusionSize = 0.00f;

    // Smoothing coefficients (time constants tuned for responsiveness vs. stability)
    float DelayTimeSmoothCoefficient = 0.0015f;  // smaller = faster tracking
    float SizeSmoothCoefficient = 0.0020f;

    // Tap layout (normalized symmetric offsets, e.g. [-0.8, -0.4, +0.4, +0.8] scaled by spread samples)
    // These are "targets"; effective spread is smoothed by SmoothedDiffusionSize.
    std::vector<float> NormalizedSymmetricOffsets;  // in [-1.0..+1.0], symmetric pairs, center excluded

    // Deterministic base set to build prime-like spacing (used when recomputing offsets)
    const int PrimeLikeSequence[8] = { 2, 3, 5, 7, 11, 13, 17, 19 }; // enough for up to 8 pairs

    // Per-channel state
    std::vector<ChannelState> Channels;

    // Safety
    bool IsPrepared = false;
};