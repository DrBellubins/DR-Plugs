#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Ducking
// - Simple envelope follower (attack / release) deriving a gain reduction applied to wet (and optionally feedback) signal.
// - Parameters:
//     Duck Amount   [0..1]  -> depth of attenuation (1 = full duck, 0 = off)
//     Duck Attack   [0..1]  -> mapped to attack time (fast at low values)
//     Duck Release  [0..1]  -> mapped to release time (slow at high values)
// - Processing strategy:
//     1. Build an absolute-value envelope of the dry input.
//     2. Envelope follows rises with Attack Alpha and falls with Release Alpha.
//     3. Gain = 1 - (DuckAmount * Envelope), optionally shaped (square-root) for musical response.
//     4. Apply Gain to wet output (and optionally to feedback write if stronger suppression desired).
//
// Mapping:
//   Attack time range :   5 ms .. 200 ms
//   Release time range:  50 ms .. 2000 ms
//
// Coefficient derivation:
//   For a one-pole smoother: y += a * (x - y)
//   Given desired time constant TauSeconds ~= TimeMs / 1000,
//   a = 1 - exp(-1 / (TauSeconds * SampleRate))
//
// Public static API mirrors other helper components used in ClusteredDiffusionDelay.
//
class Ducking
{
public:
    struct State
    {
        float Envelope = 0.0f;
    };

    // Map normalized value [0..1] to milliseconds between MinMs and MaxMs (perceptual skew toward shorter times at low values).
    static inline float MapNormalizedToMs(float NormalizedValue,
                                          float MinMs,
                                          float MaxMs)
    {
        float Clamped = juce::jlimit(0.0f, 1.0f, NormalizedValue);

        // Slight exponential bias toward shorter times (feel snappier at low knob values).
        float Biased = std::pow(Clamped, 0.45f);

        return juce::jmap(Biased, 0.0f, 1.0f, MinMs, MaxMs);
    }

    // Convert time in ms to one-pole coefficient at SampleRate.
    static inline float TimeMsToCoefficient(float TimeMs,
                                            double SampleRate)
    {
        const float MinPositive = 1.0e-6f;
        float TimeSeconds = std::max(MinPositive, TimeMs * 0.001f);
        float Alpha = 1.0f - std::exp(-1.0f / (static_cast<float>(SampleRate) * TimeSeconds));
        return juce::jlimit(0.0f, 1.0f, Alpha);
    }

    // Compute attack and release alphas for current normalized parameters.
    static inline void ComputeAttackReleaseAlphas(double SampleRate,
                                                  float NormalizedAttack,
                                                  float NormalizedRelease,
                                                  float& OutAttackAlpha,
                                                  float& OutReleaseAlpha)
    {
        const float AttackMs  = MapNormalizedToMs(NormalizedAttack, 5.0f,   200.0f);
        const float ReleaseMs = MapNormalizedToMs(NormalizedRelease, 50.0f, 2000.0f);

        OutAttackAlpha  = TimeMsToCoefficient(AttackMs,  SampleRate);
        OutReleaseAlpha = TimeMsToCoefficient(ReleaseMs, SampleRate);
    }

    static float FastRMS(float NewSampleAbs, float& RMSState)
    {
        const float Alpha = 0.0005f; // ~30 ms time constant at 44.1 kHz
        RMSState = RMSState + Alpha * (NewSampleAbs - RMSState);
        return RMSState;
    }

    // ProcessDetectorSample:
    // - Absolute value detector with light scaling.
    // - Attack uses AttackAlpha, release uses ReleaseAlpha.
    // - If input level is below SilenceThreshold, we accelerate release (smooth, not a gate).
    // - Removes previous sqrt and extra pow shaping to prevent small residual envelope from persisting.
    static inline float ProcessDetectorSample(Ducking::State& DuckState,
                                              float DetectorSample,
                                              float AttackAlpha,
                                              float ReleaseAlpha)
    {
        constexpr float SilenceThreshold = 0.0008f; // ~ -62 dB
        constexpr float FastReleaseMultiplier = 4.0f;

        float InputLevel = FastRMS(std::abs(DetectorSample) * 1.8f, DuckState.Envelope);

        // Mild pre-scale (avoid constant saturation, yet responsive for typical program ~0.2 - 0.8)
        InputLevel *= 1.5f;
        InputLevel = juce::jlimit(0.0f, 1.0f, InputLevel);

        if (InputLevel > DuckState.Envelope)
        {
            // Attack
            DuckState.Envelope = DuckState.Envelope + AttackAlpha * (InputLevel - DuckState.Envelope);
        }
        else
        {
            // Smooth release; accelerate if effectively silent (soft behavior, still exponential)
            const float AppliedReleaseAlpha = (InputLevel < SilenceThreshold
                                               ? juce::jlimit(0.0f, 1.0f, ReleaseAlpha * FastReleaseMultiplier)
                                               : ReleaseAlpha);

            DuckState.Envelope = DuckState.Envelope + AppliedReleaseAlpha * (InputLevel - DuckState.Envelope);
        }

        // Denormal hygiene + ensure true zero when effectively finished
        if (DuckState.Envelope < 1.0e-8f)
            DuckState.Envelope = 0.0f;

        return DuckState.Envelope;
    }

    // ComputeDuckGain:
    // - Depth-scaled linear attenuation: Gain = 1 - DuckAmount * Envelope.
    // - Ensures that when Envelope == 0, Gain == 1 regardless of DuckAmount.
    // - No hard mute branch; attenuation smoothly approaches silence as envelope -> 1 and duckAmount -> 1.
    static inline float ComputeDuckGain(float EnvelopeValue,
                                    float DuckAmount,
                                    float MaxAttenuationDecibels)
    {
        float ClampedAmount = juce::jlimit(0.0f, 1.0f, DuckAmount);
        float ClampedEnv    = juce::jlimit(0.0f, 1.0f, EnvelopeValue);

        // Optional shaping to make mid-level envelopes more punitive:
        // sqrt raises values (<1) -> more attenuation earlier.
        float ShapedEnv = std::sqrt(ClampedEnv);

        // Depth in dB grows with both amount and envelope.
        float DepthDecibels = ClampedAmount * ShapedEnv * MaxAttenuationDecibels;

        // Convert to linear gain (negative because attenuation).
        float LinearGain = juce::Decibels::decibelsToGain(-DepthDecibels);

        // Safety clamp (avoid denorm zone, treat near-zero as zero).
        if (LinearGain < 1.0e-6f)
            LinearGain = 0.0f;

        return LinearGain;
    }

    static inline float ComputeDuckGain(float EnvelopeValue, float DuckAmount)
    {
        return ComputeDuckGain(EnvelopeValue, DuckAmount, 60.0f);
    }

    // Reset envelope state.
    static inline void Reset(Ducking::State& DuckState)
    {
        DuckState.Envelope = 0.0f;
    }
};