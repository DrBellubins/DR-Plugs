#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Simple one-pole highpass filter (real-time safe).
// - Implemented as difference between input and a one-pole lowpass of the input:
//     hp[n] = x[n] - lpState[n]
//   where lpState[n] = lpState[n-1] + alpha * (x[n] - lpState[n-1])
// - This yields a stable, cheap highpass suitable for pre/post filtering and damping stages.
class HighpassFilter
{
public:
    HighpassFilter();
    ~HighpassFilter();

    // Prepare the filter with a given sample rate. Must be called before processing.
    void prepareToPlay(double newSampleRate);

    // Reset internal history (zero previous input/state).
    void reset();

    // Set cutoff frequency in Hz. Values are clamped to a safe range.
    void setCutoffFrequency(float newCutoffHz);

    // Process a single sample and return the highpassed output.
    float processSample(float inputSample);

    // Process an in-place block of samples.
    void processBlock(float* samplesBuffer, int numSamples);

    // Query current cutoff (Hz).
    float getCutoffFrequency() const;

private:
    // Recompute alpha for the embedded lowpass used to compute the HP output.
    void updateAlpha();

    double sampleRate = 44100.0;     // sample rate
    float cutoffHz = 20.0f;         // cutoff frequency (Hz)
    float alpha = 1.0f;             // smoothing coefficient for the internal LP
    float lpState = 0.0f;           // internal lowpass state (used to compute HP = x - lpState)
    bool isPrepared = false;        // true after prepareToPlay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HighpassFilter)
};