#include "HighpassFilter.h"

// Constructor
HighpassFilter::HighpassFilter()
{
    // Defaults left simple; prepareToPlay should be called before use.
}

// Destructor
HighpassFilter::~HighpassFilter()
{
}

// Prepare filter with the given sample rate and update internal coefficient.
void HighpassFilter::prepareToPlay(double newSampleRate)
{
    sampleRate = (newSampleRate > 0.0 ? newSampleRate : 44100.0);
    updateAlpha();
    isPrepared = true;
}

// Reset internal LP state used for HP calculation.
void HighpassFilter::reset()
{
    if (!isPrepared)
        return;

    lpState = 0.0f;
}

// Set cutoff frequency (Hz) and recompute alpha.
void HighpassFilter::setCutoffFrequency(float newCutoffHz)
{
    // Clamp between 1 Hz and Nyquist - 1 Hz
    float Nyquist = static_cast<float>(sampleRate * 0.5);
    float ClampedCutoff = juce::jlimit(1.0f, Nyquist - 1.0f, newCutoffHz);

    cutoffHz = ClampedCutoff;
    updateAlpha();
}

// Process a single sample: hp = x - lp(x)
float HighpassFilter::processSample(float inputSample)
{
    // Pass through if not prepared
    if (!isPrepared)
        return inputSample;

    // Update embedded lowpass state
    lpState = lpState + alpha * (inputSample - lpState);

    // Highpass output is the difference between input and the smoothed lowpass state
    float outputSample = inputSample - lpState;

    return outputSample;
}

// In-place block processing
void HighpassFilter::processBlock(float* samplesBuffer, int numSamples)
{
    if (!isPrepared || samplesBuffer == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
        samplesBuffer[i] = processSample(samplesBuffer[i]);
}

// Query current cutoff frequency.
float HighpassFilter::getCutoffFrequency() const
{
    return cutoffHz;
}

// Update alpha used by the internal lowpass: alpha = 1 - exp(-2*pi*fc / fs)
void HighpassFilter::updateAlpha()
{
    if (sampleRate <= 0.0)
    {
        alpha = 1.0f;
        return;
    }

    float Omega = 2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate);
    float Computed = 1.0f - std::exp(-Omega);

    alpha = juce::jlimit(0.0f, 1.0f, Computed);
}