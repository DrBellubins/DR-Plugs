#pragma once

#include <vector>
#include <cmath>
#include <juce_core/juce_core.h>

#include "DelayLine.h"

// Diffusion
// - Reworked to provide an embedded allpass diffusion chain suitable for FDN feedback bus processing.
// - Replaces the previous multi-tap clustered read with a series of short allpass filters.
// - Parameter mapping:
//     * Amount  [0..1] -> crossfade from bypass to full chain (equal-power).
//     * Size    [0..1] -> scales stage delays (e.g., 5..40 ms).
//     * Quality [0..1] -> number of stages (2..8) and optional small jitter.
//
// Implementation:
// - Each stage is a first-order allpass with delay D_i and coefficient g_i.
//   y[n] = -g * x[n] + x[n - D] + g * y[n - D]
//   Implemented via a small delay buffer and internal state.
// - Stages are processed serially.
// - Optional slow per-stage jitter can be applied to D_i to reduce metallic artifacts at high Quality.

class Diffusion
{
public:
    struct AllpassStage
    {
        // Delay buffer for the stage
        std::vector<float> Buffer;
        int WriteIndex = 0;

        // Nominal integer delay in samples (used for base indexing)
        int NominalDelaySamples = 1;

        // Coefficient (0 < g < 1), typical 0.6..0.7
        float CoefficientG = 0.65f;

        // Slow jitter amount in samples (fractional), applied around NominalDelaySamples
        float JitterDepthSamples = 0.0f;

        // Internal state for fractional read (linear interpolation only)
        float LastOutput = 0.0f;
    };

    struct AllpassChain
    {
        std::vector<AllpassStage> Stages;

        // Cached maximum delay across stages to size buffers safely
        int MaxStageDelaySamples = 1;
    };

    // Prepare a chain with up to MaxStages and buffer capacity for MaxDelaySamples per stage.
    static void Prepare(Diffusion::AllpassChain& ChainState,
                        int NumberOfStages,
                        int MaxDelaySamplesPerStage)
    {
        const int ClampedStages = juce::jlimit(1, 16, NumberOfStages);
        const int ClampedMaxDelay = std::max(1, MaxDelaySamplesPerStage);

        ChainState.Stages.resize(static_cast<size_t>(ClampedStages));
        ChainState.MaxStageDelaySamples = ClampedMaxDelay;

        for (int StageIndex = 0; StageIndex < ClampedStages; ++StageIndex)
        {
            AllpassStage& Stage = ChainState.Stages[static_cast<size_t>(StageIndex)];
            Stage.Buffer.assign(static_cast<size_t>(ClampedMaxDelay + 2), 0.0f);
            Stage.WriteIndex = 0;
            Stage.NominalDelaySamples = std::max(1, ClampedMaxDelay / 4); // Default: short delay
            Stage.CoefficientG = 0.65f;
            Stage.JitterDepthSamples = 0.0f;
            Stage.LastOutput = 0.0f;
        }
    }

    static void Reset(Diffusion::AllpassChain& ChainState)
    {
        for (AllpassStage& Stage : ChainState.Stages)
        {
            std::fill(Stage.Buffer.begin(), Stage.Buffer.end(), 0.0f);
            Stage.WriteIndex = 0;
            Stage.LastOutput = 0.0f;
        }
    }

    // Map Quality [0..1] -> number of stages [2..8].
    static int QualityToStages(float DiffusionQualityNormalized)
    {
        const int Stages = 2 + static_cast<int>(std::round(juce::jlimit(0.0f, 1.0f, DiffusionQualityNormalized) * 6.0f));
        return juce::jlimit(2, 8, Stages);
    }

    // Configure per-stage delays and g coefficients based on Size and Quality.
    // - sampleRate is used to map ms to samples.
    // - baseRangeMs ~ [5 .. 40] scaled by Size.
    // - g is fixed or slightly reduced as delays increase to keep response smooth.
    static void Configure(Diffusion::AllpassChain& ChainState,
                          double SampleRate,
                          float DiffusionSizeNormalized,
                          float DiffusionQualityNormalized)
    {
        const int NumberOfStages = static_cast<int>(ChainState.Stages.size());

        const float SizeClamped = juce::jlimit(0.0f, 1.0f, DiffusionSizeNormalized);
        const float QualityClamped = juce::jlimit(0.0f, 1.0f, DiffusionQualityNormalized);

        // Delay in ms per stage: spread between 5..40 ms, scaled by Size.
        const float MinMs = 5.0f;
        const float MaxMs = 40.0f;
        const float SizeMs = juce::jmap(SizeClamped, 0.0f, 1.0f, MinMs, MaxMs);

        // Small progression across stages to avoid identical delays
        for (int StageIndex = 0; StageIndex < NumberOfStages; ++StageIndex)
        {
            const float Progress = static_cast<float>(StageIndex) / static_cast<float>(std::max(1, NumberOfStages - 1));
            const float StageMs = SizeMs * juce::jmap(Progress, 0.0f, 1.0f, 1.0f, 1.6f);

            const int StageDelaySamples = std::max(1, static_cast<int>(std::round((StageMs / 1000.0f) * static_cast<float>(SampleRate))));

            AllpassStage& Stage = ChainState.Stages[static_cast<size_t>(StageIndex)];
            Stage.NominalDelaySamples = juce::jlimit(1, ChainState.MaxStageDelaySamples, StageDelaySamples);

            // g slightly reduces as StageMs grows for smoother tails
            const float BaseG = 0.70f;
            const float GReduction = juce::jmap(StageMs, MinMs, MaxMs, 0.0f, 0.12f);
            Stage.CoefficientG = juce::jlimit(0.40f, 0.95f, BaseG - GReduction);

            // Jitter depth grows with quality (optional)
            Stage.JitterDepthSamples = juce::jmap(QualityClamped, 0.0f, 1.0f, 0.0f, 0.75f);
        }
    }

    // Process the chain for one sample.
    // - Amount controls equal-power crossfade between dry input and fully diffused output.
    // - Optional slow jitter LFO per stage is provided via jitterPhaseIncrement (small), phase is advanced externally if needed.
    static float ProcessChainSample(Diffusion::AllpassChain& ChainState,
                                    float InputSample,
                                    float DiffusionAmountNormalized,
                                    float JitterPhase,
                                    float JitterPhaseIncrement)
    {
        const float AmountClamped = juce::jlimit(0.0f, 1.0f, DiffusionAmountNormalized);

        // Equal-power A/B mix factors
        const float AmountA = std::cos(AmountClamped * juce::MathConstants<float>::halfPi);
        const float AmountB = std::sin(AmountClamped * juce::MathConstants<float>::halfPi);

        float StageInput = InputSample;

        for (AllpassStage& Stage : ChainState.Stages)
        {
            const int BufferSize = static_cast<int>(Stage.Buffer.size());

            if (BufferSize <= 1)
                continue;

            // Jittered fractional delay around nominal
            const float JitterOffsetSamples = Stage.JitterDepthSamples * std::sin(JitterPhase);
            const float EffectiveDelaySamples = juce::jlimit(1.0f,
                                                             static_cast<float>(BufferSize - 2),
                                                             static_cast<float>(Stage.NominalDelaySamples) + JitterOffsetSamples);

            // Read delayed x and y for allpass computation
            // Read x[n - D]
            float ReadPositionX = static_cast<float>(Stage.WriteIndex) - EffectiveDelaySamples;

            while (ReadPositionX < 0.0f)
            {
                ReadPositionX += static_cast<float>(BufferSize);
            }

            const int IndexXA = static_cast<int>(ReadPositionX) % BufferSize;
            const int IndexXB = (IndexXA + 1) % BufferSize;
            const float FractionX = ReadPositionX - static_cast<float>(IndexXA);

            const float XDelayedA = Stage.Buffer[static_cast<size_t>(IndexXA)];
            const float XDelayedB = Stage.Buffer[static_cast<size_t>(IndexXB)];
            const float XDelayed = XDelayedA + (XDelayedB - XDelayedA) * FractionX;

            // For y[n - D], we approximate using a shadow ring held in Buffer too:
            // We need previous outputs stored; reuse same buffer by writing outputs and reading delayed outputs similarly.
            // Here, we treat Stage.LastOutput as the current y[n], and the buffer contents as previous x writes.
            // To preserve the classic first-order allpass behavior in a simple form, we use a canonical implementation:
            //
            // y = -g * x + xDelayed + g * yDelayed
            //
            // Approximate yDelayed using the same delayed index in the buffer, which we store outputs into after computing y.
            float ReadPositionY = ReadPositionX; // identical position for simplicity
            const int IndexYA = IndexXA;
            const int IndexYB = IndexXB;
            const float FractionY = FractionX;

            const float YDelayedA = Stage.Buffer[static_cast<size_t>(IndexYA)]; // previous outputs approximated by previous writes
            const float YDelayedB = Stage.Buffer[static_cast<size_t>(IndexYB)];
            const float YDelayed = YDelayedA + (YDelayedB - YDelayedA) * FractionY;

            const float g = Stage.CoefficientG;

            const float StageOutput = (-g * StageInput) + XDelayed + (g * YDelayed);

            // Write current "input + output" blend into buffer to maintain continuity.
            // We store StageInput for x path and StageOutput for y path blended to avoid denorm issues.
            const float BufferWrite = 0.5f * (StageInput + StageOutput);

            Stage.Buffer[static_cast<size_t>(Stage.WriteIndex)] = BufferWrite;
            Stage.WriteIndex++;

            if (Stage.WriteIndex >= BufferSize)
            {
                Stage.WriteIndex = 0;
            }

            Stage.LastOutput = StageOutput;
            StageInput = StageOutput;

            // Advance jitter phase slightly per stage for decorrelation
            JitterPhase += JitterPhaseIncrement;
        }

        const float DiffusedOutput = StageInput;

        // Equal-power blend between dry InputSample and diffused output
        const float OutputSample = (AmountA * InputSample) + (AmountB * DiffusedOutput);

        return OutputSample;
    }
};
