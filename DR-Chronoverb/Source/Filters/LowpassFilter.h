#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Simple one-pole lowpass filter (real-time safe).
// - Uses the discrete one-pole form:
//     y[n] = y[n-1] + alpha * (x[n] - y[n-1])
// - alpha is computed from cutoff frequency and sample rate:
//     alpha = 1 - exp(-2*pi*fc / fs)
// - This class is intentionally minimal and allocation-free in the audio path.
class LowpassFilter
{
public:
    LowpassFilter();
    ~LowpassFilter();

    // Prepare the filter with current sample rate. Must be called before processing.
    void prepareToPlay(double newSampleRate);

    // Reset internal state (zero history). Safe to call between runs.
    void reset();

    // Set cutoff frequency in Hz. Values will be clamped to a safe range.
    void setCutoffFrequency(float newCutoffHz);

    // Process a single sample and return filtered output.
    float processSample(float inputSample);

    // Process a contiguous block of samples in-place. samplesBuffer must be non-null.
    void processBlock(float* samplesBuffer, int numSamples);

    // Query current cutoff (Hz).
    float getCutoffFrequency() const;

    // Query the current effective smoothing coefficient alpha (0..1).
    float getAlpha() const;

private:
    // Recompute alpha after changes to sample rate or cutoff.
    void updateAlpha();

    double sampleRate = 44100.0;      // Stored sample rate
    float cutoffHz = 20000.0f;        // Cutoff frequency in Hz
    float alpha = 1.0f;              // Smoothing coefficient derived from cutoff and sample rate
    float state = 0.0f;              // Filter state (y[n-1])

    bool isPrepared = false;         // True after prepareToPlay

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LowpassFilter)
};