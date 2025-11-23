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

    // Process detector path (dry input absolute value) updating envelope.
    static inline float ProcessDetectorSample(Ducking::State& DuckState,
                                              float DetectorSample,
                                              float AttackAlpha,
                                              float ReleaseAlpha)
    {
        float InputLevel = std::abs(DetectorSample);

        if (InputLevel > DuckState.Envelope)
        {
            // Rising edge -> attack
            DuckState.Envelope = DuckState.Envelope + AttackAlpha * (InputLevel - DuckState.Envelope);
        }
        else
        {
            // Falling edge -> release
            DuckState.Envelope = DuckState.Envelope + ReleaseAlpha * (InputLevel - DuckState.Envelope);
        }

        return DuckState.Envelope;
    }

    // Translate envelope + amount to a ducking gain.
    static inline float ComputeDuckGain(float EnvelopeValue,
                                        float DuckAmount)
    {
        float ClampedAmount = juce::jlimit(0.0f, 1.0f, DuckAmount);
        float ClampedEnv    = juce::jlimit(0.0f, 1.0f, EnvelopeValue);

        // Linear attenuation depth; no square-root shaping for stronger, clearer ducking.
        float LinearGain = 1.0f - (ClampedAmount * ClampedEnv);

        return juce::jlimit(0.0f, 1.0f, LinearGain);
    }

    // Reset envelope state.
    static inline void Reset(Ducking::State& DuckState)
    {
        DuckState.Envelope = 0.0f;
    }
};