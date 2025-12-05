#pragma once

#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>

// FeedbackDelayNetwork (FDN)
// - Supports 1..8 delay lines (internally uses nearest Hadamard size: 1, 2, 4, 8).
// - Prime-length delays per line (chosen from a prime table and scaled by size).
// - Hadamard feedback matrix for orthogonal mixing (normalized).
// - Per-line damping (one-pole lowpass) in the feedback path.
// - Matrix gain controls decay (~0.5-0.7 recommended).
//
// Notes:
// - If a requested line count is not a Hadamard size, we clamp to nearest lower
//   supported size (1, 2, 4, 8).
// - Size parameter selects which prime delays are used (shorter to longer).
// - Damping is applied to each line's delayed read before mixing back via the matrix.
// - This class is self-contained (no JUCE dependencies).
class SimpleFDN
{
public:
    SimpleFDN()
    {
    }

    void Prepare(double sampleRate)
    {
        sr = (sampleRate > 0.0 ? sampleRate : 48000.0);
        clearAll();
        configured = false;
    }

    // Configure the FDN:
    // - requestedLines: 1..8
    // - size01: selects shorter/longer prime delays (0..1)
    // - matrixGain: overall feedback matrix gain (~0.5..0.7) for decay control
    void Configure(int requestedLines, float size01, float matrixGain)
    {
        const int clampedRequested = std::max(1, std::min(8, requestedLines));

        // Hadamard supported sizes: 1, 2, 4, 8
        const int hadamardSizes[4] = { 1, 2, 4, 8 };
        int targetLines = hadamardSizes[0];

        for (int index = 0; index < 4; ++index)
        {
            if (hadamardSizes[index] <= clampedRequested)
                targetLines = hadamardSizes[index];
        }

        // Build lines with prime delays according to size01.
        const float s01 = std::max(0.0f, std::min(1.0f, size01));
        const float mg = std::max(0.0f, std::min(0.95f, matrixGain));

        matrixGainScalar = mg;
        numLines = targetLines;

        lines.clear();
        lines.resize(static_cast<size_t>(numLines));

        // Prime table (in samples) roughly spanning short->long taps.
        // Choose primes near ranges appropriate for diffusion-like early reflections to ~100ms at 48k.
        // We'll select a contiguous window based on size01.
        const int primeCandidates[] =
        {
            89,   113,  149,  193,  257,  313,  431,  577,
            769,  997,  1291, 1543, 1877, 2203, 2539, 2903
        };
        const int primeCount = static_cast<int>(sizeof(primeCandidates) / sizeof(primeCandidates[0]));

        // Compute start index into primeCandidates based on size01, ensuring enough primes for numLines.
        const int maxStart = std::max(0, primeCount - numLines);
        const int startIndex = std::min(maxStart,
                                        static_cast<int>(std::floor(s01 * static_cast<float>(maxStart))));

        // Per-line damping cutoff default: will be controlled by SetDamping01
        // We keep previous damping setting; if none, set default moderate damping.
        if (!dampingInitialized)
        {
            damping01 = 0.5f;
            dampingInitialized = true;
        }

        for (int lineIndex = 0; lineIndex < numLines; ++lineIndex)
        {
            const int primeDelaySamples = primeCandidates[startIndex + lineIndex];

            Line& line = lines[static_cast<size_t>(lineIndex)];
            line.ensureBufferSize(std::max(primeDelaySamples + 8, 1024)); // ensure ring size
            line.delaySamples = primeDelaySamples;
            line.writeIndex = 0;
            line.lastDamped = 0.0f;
        }

        // Precompute Hadamard matrix of size numLines, normalized by 1/sqrt(N).
        buildHadamard();

        // Update damping coefficient from damping01
        updateDampingAlpha();

        configured = true;
    }

    // Set per-line damping amount (0..1) mapped to cutoff range.
    // We use a simple one-pole lowpass:
    // y = alpha * x + (1 - alpha) * z1
    void SetDamping01(float newDamping01)
    {
        damping01 = std::max(0.0f, std::min(1.0f, newDamping01));
        updateDampingAlpha();
    }

    // Process one input sample.
    // Returns the FDN output (sum of line reads scaled).
    float ProcessSample(float inputSample)
    {
        if (!configured || numLines <= 0)
        {
            return inputSample;
        }

        // 1) Read per-line delayed outputs and apply damping.
        tempLineOut.resize(static_cast<size_t>(numLines));
        for (int lineIndex = 0; lineIndex < numLines; ++lineIndex)
        {
            const float xDelayed = lines[static_cast<size_t>(lineIndex)].readDelay();
            // One-pole damping
            Line& line = lines[static_cast<size_t>(lineIndex)];
            const float y = dampingAlpha * xDelayed + (1.0f - dampingAlpha) * line.lastDamped;
            line.lastDamped = y;
            tempLineOut[static_cast<size_t>(lineIndex)] = y;
        }

        // 2) Mix via Hadamard feedback matrix (orthogonal mixing).
        // feedbackVec = H * tempLineOut
        tempFeedback.resize(static_cast<size_t>(numLines));
        for (int row = 0; row < numLines; ++row)
        {
            float acc = 0.0f;
            for (int col = 0; col < numLines; ++col)
            {
                acc += hadamard[row][col] * tempLineOut[static_cast<size_t>(col)];
            }
            tempFeedback[static_cast<size_t>(row)] = acc * matrixGainScalar; // apply matrix gain for decay
        }

        // 3) Distribute input to lines equally (scaled injection).
        const float inputInjection = inputSample * (1.0f / static_cast<float>(std::max(1, numLines)));

        // 4) Write new samples into lines: input + feedback
        for (int lineIndex = 0; lineIndex < numLines; ++lineIndex)
        {
            const float in = inputInjection + tempFeedback[static_cast<size_t>(lineIndex)];
            lines[static_cast<size_t>(lineIndex)].push(in);
        }

        // 5) Output: sum of delayed outputs (pre-damped or damped?) Use damped to keep tone controlled.
        float sumOut = 0.0f;
        for (int lineIndex = 0; lineIndex < numLines; ++lineIndex)
        {
            sumOut += tempLineOut[static_cast<size_t>(lineIndex)];
        }

        // Normalize by sqrt(N) to avoid growth with line count, matching Hadamard normalization.
        const float norm = 1.0f / std::sqrt(static_cast<float>(std::max(1, numLines)));
        return sumOut * norm;
    }

private:
    struct Line
    {
        std::vector<float> buffer;
        int writeIndex = 0;
        int delaySamples = 100; // prime length
        float lastDamped = 0.0f;

        void ensureBufferSize(int size)
        {
            if (static_cast<int>(buffer.size()) < size)
            {
                buffer.assign(size, 0.0f);
                writeIndex = 0;
            }
        }

        void push(float inputSample)
        {
            buffer[writeIndex] = inputSample;
            writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());
        }

        float readDelay() const
        {
            const int size = static_cast<int>(buffer.size());
            int readIndex = writeIndex - delaySamples;
            while (readIndex < 0) readIndex += size;
            readIndex %= size;
            return buffer[readIndex];
        }
    };

    double sr = 48000.0;
    int numLines = 0;
    bool configured = false;

    // Per-line state
    std::vector<Line> lines;

    // Hadamard matrix (normalized)
    std::vector<std::vector<float>> hadamard;

    // Scratch vectors
    std::vector<float> tempLineOut;
    std::vector<float> tempFeedback;

    // Parameters
    float matrixGainScalar = 0.6f; // ~0.5..0.7 recommended
    float damping01 = 0.5f;
    float dampingAlpha = 0.1f;
    bool dampingInitialized = false;

    void clearAll()
    {
        lines.clear();
        hadamard.clear();
        tempLineOut.clear();
        tempFeedback.clear();
    }

    void buildHadamard()
    {
        hadamard.assign(static_cast<size_t>(numLines), std::vector<float>(static_cast<size_t>(numLines), 0.0f));

        // Base Hadamard H1 = [1]
        hadamard[0][0] = 1.0f;

        // Recursive construction for sizes 2, 4, 8
        int currentSize = 1;
        while (currentSize < numLines)
        {
            const int nextSize = currentSize * 2;

            for (int r = 0; r < currentSize; ++r)
            {
                for (int c = 0; c < currentSize; ++c)
                {
                    const float v = hadamard[r][c];
                    // Top-left
                    hadamard[r][c] = v;
                    // Top-right
                    hadamard[r][c + currentSize] = v;
                    // Bottom-left
                    hadamard[r + currentSize][c] = v;
                    // Bottom-right
                    hadamard[r + currentSize][c + currentSize] = -v;
                }
            }

            currentSize = nextSize;
        }

        // Normalize by 1/sqrt(N)
        const float norm = 1.0f / std::sqrt(static_cast<float>(numLines));
        for (int r = 0; r < numLines; ++r)
        {
            for (int c = 0; c < numLines; ++c)
            {
                hadamard[r][c] *= norm;
            }
        }
    }

    void updateDampingAlpha()
    {
        // Map damping01 to cutoff in [500 .. 9000] Hz similar to DampingFilter mapping.
        const float cutoffHz = 500.0f + damping01 * (9000.0f - 500.0f);
        const float x = std::exp(-2.0f * 3.14159265358979323846f * cutoffHz / static_cast<float>(sr));
        dampingAlpha = 1.0f - x;
        // Clamp for stability
        dampingAlpha = std::max(0.0001f, std::min(0.9999f, dampingAlpha));
    }
};