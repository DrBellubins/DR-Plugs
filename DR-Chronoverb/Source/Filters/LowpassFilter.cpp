#include "LowpassFilter.h"

// Constructor: initialize defaults
LowpassFilter::OnePoleLowpass()
{
    // Keep trivial defaults; real initialization happens in prepareToPlay or setCutoffFrequency.
}

// Destructor: nothing special to free (vectorless, RAII)
LowpassFilter::~OnePoleLowpass()
{
}

// Prepare the filter for processing with the provided sample rate.
// This will compute the internal alpha for the current cutoff.
void LowpassFilter::prepareToPlay(double newSampleRate)
{
    // Guard and store a sensible sample rate
    sampleRate = (newSampleRate > 0.0 ? newSampleRate : 44100.0);

    // Recompute alpha with the new sample rate
    updateAlpha();

    // Mark as prepared
    isPrepared = true;
}

// Reset filter history to silence
void LowpassFilter::reset()
{
    if (!isPrepared)
        return;

    state = 0.0f;
}

// Set cutoff frequency in Hz. The value is clamped to a safe audible range.
void LowpassFilter::setCutoffFrequency(float newCutoffHz)
{
    // Avoid negative or zero cutoff; clamp between 1 Hz and Nyquist- epsilon
    float Nyquist = static_cast<float>(sampleRate * 0.5);
    float ClampedCutoff = juce::jlimit(1.0f, Nyquist - 1.0f, newCutoffHz);

    cutoffHz = ClampedCutoff;

    // Update alpha immediately so subsequent samples use the new coefficient.
    updateAlpha();
}

// Process a single input sample and return the lowpassed output.
float LowpassFilter::processSample(float inputSample)
{
    // If not prepared, just pass input through
    if (!isPrepared)
        return inputSample;

    // One-pole lowpass update: y += alpha * (x - y)
    state = state + alpha * (inputSample - state);

    return state;
}

// Process an in-place block of float samples.
void LowpassFilter::processBlock(float* samplesBuffer, int numSamples)
{
    if (!isPrepared || samplesBuffer == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
        samplesBuffer[i] = processSample(samplesBuffer[i]);
}

// Return the configured cutoff frequency in Hz.
float LowpassFilter::getCutoffFrequency() const
{
    return cutoffHz;
}

// Return the internal alpha smoothing coefficient.
float LowpassFilter::getAlpha() const
{
    return alpha;
}

// Compute alpha = 1 - exp(-2*pi*fc / fs)
// Clamped to [0..1] for numerical safety.
void LowpassFilter::updateAlpha()
{
    // Protect against invalid sample rate or cutoff
    if (sampleRate <= 0.0)
    {
        alpha = 1.0f;
        return;
    }

    // Compute the continuous-time to discrete alpha mapping
    float Omega = 2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate);
    float Computed = 1.0f - std::exp(-Omega);

    // Clamp to valid range
    alpha = juce::jlimit(0.0f, 1.0f, Computed);
}