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
        std::vector<float> Buffer;
        int WriteIndex = 0;

        int NominalDelaySamples = 1;
        float CoefficientG = 0.65f;

        float JitterDepthSamples = 0.0f;
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
            Stage.NominalDelaySamples = std::max(1, ClampedMaxDelay / 4);
            Stage.CoefficientG = 0.65f;
            Stage.JitterDepthSamples = 0.0f;
        }
    }

    static void Reset(Diffusion::AllpassChain& ChainState)
    {
        for (AllpassStage& Stage : ChainState.Stages)
        {
            std::fill(Stage.Buffer.begin(), Stage.Buffer.end(), 0.0f);
            Stage.WriteIndex = 0;
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
    // y = g * w + d, where w = x - g * d, and d = delayed(w) (store w in buffer).
    static float ProcessChainSample(Diffusion::AllpassChain& ChainState,
                                float InputSample,
                                float DiffusionAmountNormalized,
                                float JitterPhase,
                                float JitterPhaseIncrement)
    {
        const float AmountClamped = juce::jlimit(0.0f, 1.0f, DiffusionAmountNormalized);

        const float AmountA = std::cos(AmountClamped * juce::MathConstants<float>::halfPi);
        const float AmountB = std::sin(AmountClamped * juce::MathConstants<float>::halfPi);

        float StageInput = InputSample;

        for (AllpassStage& Stage : ChainState.Stages)
        {
            const int BufferSize = static_cast<int>(Stage.Buffer.size());

            if (BufferSize <= 2)
                continue;

            const float JitterOffsetSamples = Stage.JitterDepthSamples * std::sin(JitterPhase);
            const float EffectiveDelaySamples = juce::jlimit(1.0f,
                                                             static_cast<float>(BufferSize - 2),
                                                             static_cast<float>(Stage.NominalDelaySamples) + JitterOffsetSamples);

            // Read delayed value d = w[n - D] with linear interpolation
            float ReadPosition = static_cast<float>(Stage.WriteIndex) - EffectiveDelaySamples;

            while (ReadPosition < 0.0f)
                ReadPosition += static_cast<float>(BufferSize);

            const int IndexA = static_cast<int>(ReadPosition) % BufferSize;
            const int IndexB = (IndexA + 1) % BufferSize;
            const float Fraction = ReadPosition - static_cast<float>(IndexA);

            const float DelayedA = Stage.Buffer[static_cast<size_t>(IndexA)];
            const float DelayedB = Stage.Buffer[static_cast<size_t>(IndexB)];
            const float d = DelayedA + (DelayedB - DelayedA) * Fraction;

            const float g = Stage.CoefficientG;

            // Canonical first-order allpass using 'w' stored in the buffer
            const float w = StageInput - g * d;
            const float y = g * w + d;

            // Write w and advance
            Stage.Buffer[static_cast<size_t>(Stage.WriteIndex)] = w;

            Stage.WriteIndex++;

            if (Stage.WriteIndex >= BufferSize)
                Stage.WriteIndex = 0;

            StageInput = y;

            JitterPhase += JitterPhaseIncrement;
        }

        const float DiffusedOutput = StageInput;
        const float OutputSample = (AmountA * InputSample) + (AmountB * DiffusedOutput);

        return OutputSample;
    }
};
