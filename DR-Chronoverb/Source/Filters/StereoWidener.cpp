#include "StereoWidener.h"

//==============================================================================
StereoWidener::StereoWidener()
{
    // Minimal construction; real allocation occurs in prepareToPlay.
}

StereoWidener::~StereoWidener()
{
    // Vector destructors free memory automatically.
}

void StereoWidener::prepareToPlay(double newSampleRate, float haasMaxMilliseconds)
{
    // Store a sensible sample rate
    sampleRate = (newSampleRate > 0.0 ? newSampleRate : 44100.0);

    // Ensure at least a tiny buffer is allocated
    haasMaxDelaySamples = std::max(1, millisecondsToSamples(juce::jmax(0.0f, haasMaxMilliseconds)));

    // Allocate and zero the Haas buffers (one extra slot to simplify wrap math)
    haasBufferLeft.assign(static_cast<size_t>(haasMaxDelaySamples + 1), 0.0f);
    haasBufferRight.assign(static_cast<size_t>(haasMaxDelaySamples + 1), 0.0f);

    haasWriteIndex = 0;

    isPrepared = true;
}

void StereoWidener::reset()
{
    if (!isPrepared)
        return;

    std::fill(haasBufferLeft.begin(), haasBufferLeft.end(), 0.0f);
    std::fill(haasBufferRight.begin(), haasBufferRight.end(), 0.0f);

    haasWriteIndex = 0;
}

void StereoWidener::setStereoWidth(float newWidth)
{
    // Keep the width in a safe range and store atomically
    float clamped = juce::jlimit(-1.0f, 1.0f, newWidth);
    targetWidth.store(clamped, std::memory_order_relaxed);
}

float StereoWidener::getStereoWidth() const
{
    return targetWidth.load(std::memory_order_relaxed);
}

float StereoWidener::readFromCircularBuffer(const std::vector<float>& buffer,
                                            int writeIndex,
                                            float delayInSamples)
{
    const int bufferSize = static_cast<int>(buffer.size());

    if (bufferSize <= 1)
        return 0.0f;

    // read position = writeIndex - delayInSamples (writeIndex points to the slot we'll write next)
    float readPosition = static_cast<float>(writeIndex) - delayInSamples;

    // wrap into [0..bufferSize)
    while (readPosition < 0.0f)
        readPosition += static_cast<float>(bufferSize);

    int indexA = static_cast<int>(readPosition) % bufferSize;
    int indexB = (indexA + 1) % bufferSize;
    float frac = readPosition - static_cast<float>(indexA);

    const float sampleA = buffer[static_cast<size_t>(indexA)];
    const float sampleB = buffer[static_cast<size_t>(indexB)];

    // linear interpolation
    return sampleA + (sampleB - sampleA) * frac;
}

void StereoWidener::processBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (!isPrepared)
        return;

    const int numChannels = audioBuffer.getNumChannels();
    const int numSamples = audioBuffer.getNumSamples();

    // Read current target width once per block
    const float width = targetWidth.load(std::memory_order_relaxed);

    // Positive width -> Haas widening applied to right channel (left unchanged)
    if (width > 0.0f)
    {
        // Compute fractional Haas delay in samples. Range: (0 .. haasMaxDelaySamples-1)
        const float maxDelayF = static_cast<float>(std::max(1, haasMaxDelaySamples));
        const float haasDelaySamples = width * (maxDelayF - 1.0f);

        // We'll maintain Haas buffers even for mono input to keep state consistent.
        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            // Get input sample(s)
            float leftIn = 0.0f;
            float rightIn = 0.0f;

            if (numChannels > 0)
                leftIn = audioBuffer.getReadPointer(0)[sampleIndex];

            if (numChannels > 1)
                rightIn = audioBuffer.getReadPointer(1)[sampleIndex];
            else
                rightIn = leftIn; // for mono treat right as same as left when populating buffers

            // Write current wet samples into the per-channel Haas circular buffers
            haasBufferLeft[static_cast<size_t>(haasWriteIndex)] = leftIn;
            haasBufferRight[static_cast<size_t>(haasWriteIndex)] = rightIn;

            // Read delayed right channel (fractional) and apply it as the output for the right channel.
            // Left channel remains the immediate sample (no delay) to preserve precedence.
            float delayedRight = readFromCircularBuffer(haasBufferRight, haasWriteIndex, haasDelaySamples);

            if (numChannels > 0)
                audioBuffer.getWritePointer(0)[sampleIndex] = leftIn;

            if (numChannels > 1)
                audioBuffer.getWritePointer(1)[sampleIndex] = delayedRight;
            else
                audioBuffer.getWritePointer(0)[sampleIndex] = delayedRight; // mono -> overwrite with delayed value

            // advance write index and wrap
            haasWriteIndex++;

            if (haasWriteIndex >= static_cast<int>(haasBufferLeft.size()))
                haasWriteIndex = 0;
        }

        return;
    }

    // Non-positive width (width == 0 or negative) -> mid/side reduction (narrowing) or passthrough
    if (width <= 0.0f)
    {
        // sideScale ranges from 0.0 (width == -1 -> mono) to 1.0 (width == 0 -> unchanged)
        const float sideScale = 1.0f + width; // width negative reduces side

        for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
        {
            if (numChannels >= 2)
            {
                // Read stereo inputs
                const float leftIn = audioBuffer.getReadPointer(0)[sampleIndex];
                const float rightIn = audioBuffer.getReadPointer(1)[sampleIndex];

                // Convert to mid/side
                const float mid = 0.5f * (leftIn + rightIn);
                const float side = 0.5f * (leftIn - rightIn);

                // Scale side and convert back to LR
                const float newLeft = mid + side * sideScale;
                const float newRight = mid - side * sideScale;

                audioBuffer.getWritePointer(0)[sampleIndex] = newLeft;
                audioBuffer.getWritePointer(1)[sampleIndex] = newRight;
            }
            else if (numChannels == 1)
            {
                // Mono: no audible effect from mid/side scaling, but maintain Haas buffer state.
                const float monoIn = audioBuffer.getReadPointer(0)[sampleIndex];

                // For consistency, we still write into Haas buffers (keeps continuity if user switches positive later)
                haasBufferLeft[static_cast<size_t>(haasWriteIndex)] = monoIn;
                haasBufferRight[static_cast<size_t>(haasWriteIndex)] = monoIn;

                // No change to output for width <= 0 (keep mono input)
                audioBuffer.getWritePointer(0)[sampleIndex] = monoIn;

                haasWriteIndex++;

                if (haasWriteIndex >= static_cast<int>(haasBufferLeft.size()))
                    haasWriteIndex = 0;
            }
        }

        return;
    }
}