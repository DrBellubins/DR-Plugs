#include "AllpassFilter.h"

//==============================================================================
AllpassFilter::AllpassFilter()
{
    // Default ctor keeps minimal state; actual allocation happens in prepareToPlay.
}

AllpassFilter::~AllpassFilter()
{
    // Default dtor; vector will free automatically.
}

void AllpassFilter::prepareToPlay(double newSampleRate, int maximumDelaySamples)
{
    // Store sample rate for completeness.
    sampleRate = (newSampleRate > 0.0 ? newSampleRate : 44100.0);

    // Ensure we allocate at least a small buffer.
    bufferSize = std::max(2, maximumDelaySamples);

    // Allocate delay buffer and clear it.
    delayBuffer.assign(static_cast<size_t>(bufferSize), 0.0f);

    // Reset indices and state.
    writeIndex = 0;
    delayInSamples = std::min(delayInSamples, static_cast<float>(bufferSize - 1));
    isPrepared = true;
}

void AllpassFilter::reset()
{
    if (!isPrepared)
        return;

    // Zero the delay buffer to remove previous history.
    std::fill(delayBuffer.begin(), delayBuffer.end(), 0.0f);

    // Reset index and internal states.
    writeIndex = 0;
}

void AllpassFilter::setDelayInSamples(float newDelayInSamples)
{
    // Clamp the requested delay to [1 .. bufferSize - 1] to ensure valid read points.
    if (!isPrepared)
    {
        // If not prepared yet, just store a clamped value relative to a sensible minimum.
        delayInSamples = std::max(1.0f, newDelayInSamples);
        return;
    }

    float Clamped = juce::jlimit(1.0f, static_cast<float>(std::max(2, bufferSize) - 1), newDelayInSamples);
    delayInSamples = Clamped;
}

void AllpassFilter::setFeedbackCoefficient(float newFeedbackCoefficient)
{
    // Keep coefficient in a safe range to avoid instability.
    // Values near +/-1.0 cause very long (or unstable) decay; clamp slightly inside the unit circle.
    feedbackCoefficient = juce::jlimit(-0.9995f, 0.9995f, newFeedbackCoefficient);
}

float AllpassFilter::processSample(float inputSample)
{
    // If not prepared, pass input through (no processing).
    if (!isPrepared)
        return inputSample;

    // Read the delayed entry D from the circular buffer using fractional delay support.
    // readDelayedSample expects a "look-back" value (how many samples behind writeIndex).
    float DelayedEntry = readDelayedSample(delayInSamples);

    // Compute output using the allpass equation: y = -g * x + D
    float OutputSample = -feedbackCoefficient * inputSample + DelayedEntry;

    // Store the value (x + g * y) into the buffer at the write position so future reads produce the correct equation:
    // buffer[write] = x + g * y  => when read back later D = x[n-M] + g*y[n-M]
    float ToWrite = inputSample + feedbackCoefficient * OutputSample;
    writeBufferSample(ToWrite);

    // Advance the circular buffer write pointer for next sample.
    advanceWriteIndex();

    // Return the computed allpass output.
    return OutputSample;
}

void AllpassFilter::processBlock(float* samples, int numSamples)
{
    if (!isPrepared || samples == nullptr || numSamples <= 0)
        return;

    // Process each sample in-place using the single-sample API.
    for (int SampleIndex = 0; SampleIndex < numSamples; ++SampleIndex)
        samples[SampleIndex] = processSample(samples[SampleIndex]);
}

float AllpassFilter::getDelayInSamples() const
{
    return delayInSamples;
}

float AllpassFilter::getFeedbackCoefficient() const
{
    return feedbackCoefficient;
}

//==============================================================================
float AllpassFilter::readDelayedSample(float readPositionRelativeToWrite) const
{
    // Guard: if buffer too small, return zero.
    if (bufferSize <= 1)
        return 0.0f;

    // Compute the (floating) absolute read position relative to the buffer.
    // writeIndex points to the slot that will be written next, so the most recent sample is at writeIndex-1.
    float ReadPos = static_cast<float>(writeIndex) - readPositionRelativeToWrite;

    // Wrap into [0..bufferSize)
    while (ReadPos < 0.0f)
        ReadPos += static_cast<float>(bufferSize);

    // Determine indices for linear interpolation
    int IndexA = static_cast<int>(ReadPos) % bufferSize;
    int IndexB = (IndexA + 1) % bufferSize;
    float Fraction = ReadPos - static_cast<float>(IndexA);

    // Fetch samples
    const float SampleA = delayBuffer[static_cast<size_t>(IndexA)];
    const float SampleB = delayBuffer[static_cast<size_t>(IndexB)];

    // Linear interpolation between A and B
    return SampleA + (SampleB - SampleA) * Fraction;
}

void AllpassFilter::writeBufferSample(float value)
{
    if (bufferSize <= 0)
        return;

    // Write at the current write index (overwriting the oldest sample).
    delayBuffer[static_cast<size_t>(writeIndex)] = value;
}

void AllpassFilter::advanceWriteIndex()
{
    // Increment and wrap writeIndex
    writeIndex++;

    if (writeIndex >= bufferSize)
        writeIndex = 0;
}