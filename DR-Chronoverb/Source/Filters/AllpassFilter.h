#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// A compact fractional-delay Allpass filter useful as a building block for diffusion networks.
// Implementation notes:
// - Implements the classic feed-forward / feed-back allpass structure:
//     y[n] = -g * x[n] + x[n - M] + g * y[n - M]
//   where g is the feedback coefficient and M is the delay in samples.
// - Internally the delay line stores the quantity (x + g * y) so a single circular buffer suffices.
// - Fractional delays are supported via linear interpolation when reading the delayed sample.
// - This class is intentionally small and real-time safe (no allocations in process path after prepare).
class AllpassFilter
{
public:
    AllpassFilter();
    ~AllpassFilter();

    // Prepare the filter for audio processing.
    // newSampleRate: sample rate in Hz (stored for possible conversions; not strictly required here).
    // maximumDelaySamples: the maximum integer size of the internal delay buffer to allocate.
    void prepareToPlay(double newSampleRate, int maximumDelaySamples);

    // Reset internal state and clear buffers. Safe to call between runs.
    void reset();

    // Set desired delay in samples (can be fractional). This value will be clamped to [1 .. maximumDelaySamples-1].
    void setDelayInSamples(float delayInSamples);

    // Set the allpass feedback coefficient 'g'. Typical range is (-0.999 .. +0.999).
    void setFeedbackCoefficient(float feedbackCoefficient);

    // Process a single sample and return the allpass output.
    // inputSample: the current input sample.
    float processSample(float inputSample);

    // Process an interleaved block of samples in-place. Useful for single-channel buffers.
    // samples: pointer to float samples buffer.
    // numSamples: number of samples to process.
    void processBlock(float* samples, int numSamples);

    // Query helpers
    float getDelayInSamples() const;
    float getFeedbackCoefficient() const;

private:
    // Internal read of the delay buffer with linear interpolation for fractional delays.
    // readPositionRelativeToWrite: positive number of samples to look back (e.g., delayInSamples).
    float readDelayedSample(float readPositionRelativeToWrite) const;

    // Write a raw value into the circular buffer at the current write index (no interpolation).
    void writeBufferSample(float value);

    // Advance the write index (wraps).
    void advanceWriteIndex();

    // Members
    double sampleRate = 44100.0;                 // sample rate stored for reference
    std::vector<float> delayBuffer;              // circular buffer storing (x + g * y) values
    int bufferSize = 1;                          // actual size of delayBuffer
    int writeIndex = 0;                          // current write position in circular buffer

    float delayInSamples = 1.0f;                 // desired delay (may be fractional)
    float feedbackCoefficient = 0.0f;            // allpass coefficient 'g'

    bool isPrepared = false;                     // flag set after prepareToPlay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllpassFilter)
};