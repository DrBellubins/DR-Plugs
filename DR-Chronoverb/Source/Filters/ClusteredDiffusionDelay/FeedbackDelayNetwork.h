#pragma once

#include <vector>
#include <array>
#include <memory>
#include <cmath>
#include <juce_core/juce_core.h>

#include "DelayLine.h"
#include "Lowpass.h"

// FeedbackDelayNetwork
// - Lightweight N-line FDN core with a fixed orthonormal mixing matrix (Hadamard-like for powers of two).
// - Provides per-line delay buffers, a unitary feedback mixing step, and bus-level damping.
// - Designed to be driven by an external diffuser (e.g., Diffusion::AllpassChain) in the feedback bus.
//
// Usage pattern per sample (mono or stereo treated independently):
//   1) Read the current outputs from all lines -> WetSum (simple equal mix or custom gains).
//   2) Build FeedbackBus = Damped( Diffuser( WetSum ) ).
//   3) Distribute FeedbackBus to lines via unitary matrix and write to line inputs (optionally add dry input).
//   4) Advance write index (done by DelayLine::Write).
//
// Notes:
// - Prepare() must be called with the maximum required delay in samples for all lines.
// - SetLineDelays() assigns individual delay lengths (in samples) per line; fractional reads supported.
// - The feedback matrix is orthonormal (energy-preserving), ensuring stability when diffusion/damping are reasonable.
// - Damping uses a simple one-pole LPF on the bus. You may also run damping per-line if preferred.

class FeedbackDelayNetwork
{
public:
    struct LineState
    {
        DelayLine::State Delay;
        float DelayLengthSamples = 1.0f;   // Read-back delay for this line
        float OutputTapGain = 1.0f;        // Mixing gain for wet sum
    };

    struct State
    {
        std::vector<LineState> Lines;

        // Fixed unitary mixing matrix. Stored as row-major [N x N].
        // For N not a power-of-two, we fall back to a Householder-like matrix.
        std::vector<float> FeedbackMatrix;

        // Bus damping (LPF state).
        Lowpass::State BusDampingLPF;

        // Cached for runtime
        int NumberOfLines = 0;
        int MaxDelayBufferSamples = 0;
    };

    // Prepare network with the requested number of lines and buffer size.
    static void Prepare(FeedbackDelayNetwork::State& FDNState,
                        int NumberOfLines,
                        int MaxDelayBufferSamples)
    {
        FDNState.NumberOfLines = std::max(1, NumberOfLines);
        FDNState.MaxDelayBufferSamples = std::max(1, MaxDelayBufferSamples);

        FDNState.Lines.resize(static_cast<size_t>(FDNState.NumberOfLines));

        for (int LineIndex = 0; LineIndex < FDNState.NumberOfLines; ++LineIndex)
        {
            DelayLine::Prepare(FDNState.Lines[static_cast<size_t>(LineIndex)].Delay, FDNState.MaxDelayBufferSamples);
            FDNState.Lines[static_cast<size_t>(LineIndex)].DelayLengthSamples = 1.0f;
            FDNState.Lines[static_cast<size_t>(LineIndex)].OutputTapGain = 1.0f;
        }

        // Build an orthonormal mixing matrix.
        buildUnitaryMatrix(FDNState.FeedbackMatrix, FDNState.NumberOfLines);

        // Reset bus damping.
        Lowpass::Reset(FDNState.BusDampingLPF);
    }

    static void Reset(FeedbackDelayNetwork::State& FDNState)
    {
        for (int LineIndex = 0; LineIndex < FDNState.NumberOfLines; ++LineIndex)
        {
            DelayLine::Reset(FDNState.Lines[static_cast<size_t>(LineIndex)].Delay);
        }

        Lowpass::Reset(FDNState.BusDampingLPF);
    }

    // Assign per-line delays (in samples). Size of DelayLengths must match NumberOfLines.
    static void SetLineDelays(FeedbackDelayNetwork::State& FDNState,
                              const std::vector<float>& DelayLengthsInSamples)
    {
        const int NumberOfLines = FDNState.NumberOfLines;

        jassert(static_cast<int>(DelayLengthsInSamples.size()) == NumberOfLines);

        for (int LineIndex = 0; LineIndex < NumberOfLines; ++LineIndex)
        {
            FDNState.Lines[static_cast<size_t>(LineIndex)].DelayLengthSamples =
                std::max(1.0f, DelayLengthsInSamples[static_cast<size_t>(LineIndex)]);
        }
    }

    // Optional: assign per-line output gains for the wet mix (defaults to 1.0).
    static void SetLineOutputGains(FeedbackDelayNetwork::State& FDNState,
                                   const std::vector<float>& OutputGains)
    {
        const int NumberOfLines = FDNState.NumberOfLines;
        jassert(static_cast<int>(OutputGains.size()) == NumberOfLines);

        for (int LineIndex = 0; LineIndex < NumberOfLines; ++LineIndex)
        {
            FDNState.Lines[static_cast<size_t>(LineIndex)].OutputTapGain = OutputGains[static_cast<size_t>(LineIndex)];
        }
    }

    // Read the summed wet output across all lines.
    // If NormalizeByLineCount is true, divides by N to keep level consistent.
    static float ReadWetSum(const FeedbackDelayNetwork::State& FDNState,
                            bool NormalizeByLineCount)
    {
        float WetSum = 0.0f;

        for (int LineIndex = 0; LineIndex < FDNState.NumberOfLines; ++LineIndex)
        {
            const LineState& Line = FDNState.Lines[static_cast<size_t>(LineIndex)];
            const float Tap = DelayLine::Read(Line.Delay, Line.DelayLengthSamples);
            WetSum += (Tap * Line.OutputTapGain);
        }

        if (NormalizeByLineCount && FDNState.NumberOfLines > 0)
        {
            WetSum /= static_cast<float>(FDNState.NumberOfLines);
        }

        return WetSum;
    }

    // Apply damping to the feedback bus sample (LPF one-pole).
    static float DampenBusSample(FeedbackDelayNetwork::State& FDNState,
                                 float InputBusSample,
                                 float DampingAlpha)
    {
        return Lowpass::ProcessSample(FDNState.BusDampingLPF, InputBusSample, DampingAlpha);
    }

    // Distribute a single bus sample across lines via unitary matrix and write to delay buffers.
    // Optionally add the dry input to the line write to realize "input + feedback".
    static void WriteFeedbackDistributed(FeedbackDelayNetwork::State& FDNState,
                                         float FeedbackBusSample,
                                         float DryInputSample)
    {
        const int N = FDNState.NumberOfLines;

        if (N <= 0)
            return;

        // Gather current outputs (y = current delayed outputs)
        TempLineBuffer.resize(static_cast<size_t>(N));

        for (int LineIndex = 0; LineIndex < N; ++LineIndex)
        {
            const LineState& Line = FDNState.Lines[static_cast<size_t>(LineIndex)];
            TempLineBuffer[static_cast<size_t>(LineIndex)] = DelayLine::Read(Line.Delay, Line.DelayLengthSamples);
        }

        // Compute distributed feedback for each line: sum_j (M[i,j] * FeedbackBusSample)
        // Since FeedbackBusSample is scalar, distribution reduces to M_row_sum[i] * FeedbackBusSample.
        // To preserve energy, we use the matrix rows as unit-length; row sum is used directly.
        RowSumBuffer.resize(static_cast<size_t>(N));

        const float* MatrixData = FDNState.FeedbackMatrix.data();

        for (int RowIndex = 0; RowIndex < N; ++RowIndex)
        {
            float RowSum = 0.0f;

            const int RowOffset = RowIndex * N;

            for (int ColumnIndex = 0; ColumnIndex < N; ++ColumnIndex)
            {
                RowSum += MatrixData[static_cast<size_t>(RowOffset + ColumnIndex)];
            }

            RowSumBuffer[static_cast<size_t>(RowIndex)] = RowSum;
        }

        // Write per-line: input + distributed feedback
        for (int LineIndex = 0; LineIndex < N; ++LineIndex)
        {
            const float DistributedFeedbackForLine = FeedbackBusSample * RowSumBuffer[static_cast<size_t>(LineIndex)];
            const float LineWriteSample = DryInputSample + DistributedFeedbackForLine;

            DelayLine::Write(FDNState.Lines[static_cast<size_t>(LineIndex)].Delay, LineWriteSample);
        }
    }

    // Convenience: single-step process for one input sample.
    // Returns WetSum for mixing. Caller applies dry/wet mix externally.
    // Steps:
    //   WetSum = ReadWetSum()
    //   FeedbackBus = DiffusedBusSample (provided by caller) -> DampenBusSample()
    //   WriteFeedbackDistributed(FeedbackBus, DryInputSample)
    static float ProcessOneSample(FeedbackDelayNetwork::State& FDNState,
                                  float DryInputSample,
                                  float DiffusedBusSample,
                                  float DampingAlpha,
                                  bool NormalizeWetSumByLineCount)
    {
        const float WetSumBefore = ReadWetSum(FDNState, NormalizeWetSumByLineCount);

        const float DampedBus = DampenBusSample(FDNState, DiffusedBusSample, DampingAlpha);

        WriteFeedbackDistributed(FDNState, DampedBus, DryInputSample);

        return WetSumBefore;
    }

private:
    // Build a unitary-like mixing matrix:
    // - If N is power-of-two up to 8, use a normalized Hadamard matrix.
    // - Otherwise, fall back to a Householder reflection based on a constant vector.
    static void buildUnitaryMatrix(std::vector<float>& OutMatrix,
                                   int NumberOfLines)
    {
        OutMatrix.assign(static_cast<size_t>(NumberOfLines * NumberOfLines), 0.0f);

        auto isPowerOfTwo = [](int Value) -> bool
        {
            return Value > 0 && (Value & (Value - 1)) == 0;
        };

        if (isPowerOfTwo(NumberOfLines) && NumberOfLines <= 8)
        {
            // Construct Hadamard matrix H_N recursively, normalized by sqrt(N).
            std::vector<float> H;
            buildHadamard(H, NumberOfLines);

            const float Normalization = 1.0f / std::sqrt(static_cast<float>(NumberOfLines));

            for (size_t Index = 0; Index < H.size(); ++Index)
            {
                OutMatrix[Index] = H[Index] * Normalization;
            }
        }
        else
        {
            // Householder-like matrix: I - 2 * (v v^T) / (v^T v)
            // Choose v as all-ones to get a reflection across the mean subspace.
            const float Nf = static_cast<float>(NumberOfLines);
            const float Scale = 2.0f / Nf;

            for (int RowIndex = 0; RowIndex < NumberOfLines; ++RowIndex)
            {
                for (int ColumnIndex = 0; ColumnIndex < NumberOfLines; ++ColumnIndex)
                {
                    const bool IsDiagonal = (RowIndex == ColumnIndex);
                    const float Identity = IsDiagonal ? 1.0f : 0.0f;

                    const float HouseholderTerm = Scale; // since v_i = 1, v_j = 1, => 2/N
                    OutMatrix[static_cast<size_t>(RowIndex * NumberOfLines + ColumnIndex)] = Identity - HouseholderTerm;
                }
            }
        }
    }

    static void buildHadamard(std::vector<float>& OutMatrix,
                              int Size)
    {
        // Base H_1
        OutMatrix.clear();
        OutMatrix.push_back(1.0f);

        int CurrentSize = 1;

        while (CurrentSize < Size)
        {
            const int NewSize = CurrentSize * 2;
            std::vector<float> Next(static_cast<size_t>(NewSize * NewSize), 0.0f);

            for (int RowIndex = 0; RowIndex < CurrentSize; ++RowIndex)
            {
                for (int ColumnIndex = 0; ColumnIndex < CurrentSize; ++ColumnIndex)
                {
                    const float Value = OutMatrix[static_cast<size_t>(RowIndex * CurrentSize + ColumnIndex)];

                    // Top-left
                    Next[static_cast<size_t>(RowIndex * NewSize + ColumnIndex)] = Value;

                    // Top-right
                    Next[static_cast<size_t>(RowIndex * NewSize + (ColumnIndex + CurrentSize))] = Value;

                    // Bottom-left
                    Next[static_cast<size_t>((RowIndex + CurrentSize) * NewSize + ColumnIndex)] = Value;

                    // Bottom-right (negated)
                    Next[static_cast<size_t>((RowIndex + CurrentSize) * NewSize + (ColumnIndex + CurrentSize))] = -Value;
                }
            }

            OutMatrix.swap(Next);
            CurrentSize = NewSize;
        }
    }

    // Scratch buffers (allocated once to avoid per-sample allocations)
    static inline std::vector<float> TempLineBuffer;
    static inline std::vector<float> RowSumBuffer;
};
