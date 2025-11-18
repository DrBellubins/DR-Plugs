#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <atomic>

// StereoWidener
// - Provides a small, real-time-safe stereo widener utility.
// - Behavior:
//     * width in [-1.0 .. 0.0] => stereo narrower by scaling the side channel (mid/side reduction).
//       width == -1.0 => fully mono (side scaled to 0).
//     * width == 0.0 => no change.
//     * width in (0.0 .. 1.0] => stereo widened via a Haas-style short delay applied to the right channel.
//       the maximum Haas delay (ms) is configured in prepareToPlay.
// - Real-time safe: no allocations in the processing path after prepareToPlay.
// - Use setStereoWidth(...) from your audio thread / parameter callbacks (this is atomic).
class StereoWidener
{
public:
    StereoWidener();
    ~StereoWidener();

    // Prepare the widener for processing. Must be called from the non-realtime context
    // (e.g., AudioProcessor::prepareToPlay) before calling processBlock.
    // newSampleRate: sample rate in Hz.
    // haasMaxMilliseconds: maximum Haas delay window in milliseconds (default 40 ms).
    void prepareToPlay(double newSampleRate, float haasMaxMilliseconds = 40.0f);

    // Reset internal buffers and states (zeros the Haas buffers).
    void reset();

    // Set desired stereo width in [-1.0 .. +1.0].
    // - Negative values narrow (mid/side reduction).
    // - Positive values widen (Haas delay on right channel).
    void setStereoWidth(float newWidth);

    // Process an in-place audio buffer. Works for mono or stereo buffers.
    // For mono buffers this is a no-op other than maintaining internal Haas state.
    void processBlock(juce::AudioBuffer<float>& audioBuffer);

    // Query current target width
    float getStereoWidth() const;

private:
    // Read a fractional delay sample from a circular buffer with linear interpolation.
    static float readFromCircularBuffer(const std::vector<float>& buffer,
                                        int writeIndex,
                                        float delayInSamples);

    // Convert milliseconds to samples using configured sample rate.
    inline int millisecondsToSamples(float milliseconds) const
    {
        return static_cast<int>(std::ceil((milliseconds / 1000.0f) * static_cast<float>(sampleRate)));
    }

    double sampleRate = 44100.0;
    int haasMaxDelaySamples = 1;                 // size in samples (allocated length)
    std::vector<float> haasBufferLeft;           // circular buffer for left channel Haas staging
    std::vector<float> haasBufferRight;          // circular buffer for right channel Haas staging
    int haasWriteIndex = 0;                      // shared write index for both Haas buffers

    std::atomic<float> targetWidth { 0.0f };     // [-1..1], atomic for thread-safe updates
    bool isPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StereoWidener)
};