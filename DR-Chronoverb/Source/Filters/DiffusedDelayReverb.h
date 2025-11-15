#pragma once

#include <array>
#include <vector>

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_dsp/maths/juce_Matrix.h"

/**
 * @class DiffusedDelayReverb
 * @brief A modular audio processor that combines delay and algorithmic reverb
 *        using a diffused feedback delay network (FDN) architecture.
 *
 * This class implements a unified delay/reverb effect inspired by plugins
 * like Deelay by Sixth Sample. It supports:
 * - Diffusion Amount: 0% = pure delay, 100% = full reverb
 * - Diffusion Size: Controls the scale of pre-diffusion (room size)
 * - Diffusion Quality: Low = chaotic/granular delay, High = lush reverb
 *
 * The design uses:
 * 1. A pre-diffusion stage (allpass network) before the FDN
 * 2. A 4-channel Feedback Delay Network with prime delays and feedback matrix
 * 3. Parameter smoothing and dynamic buffer resizing
 *
 * All operations are real-time safe and use circular buffers.
 */
class DiffusedDelayReverb
{
public:
    DiffusedDelayReverb();
    ~DiffusedDelayReverb() = default;

    /**
     * @brief Prepares the processor for playback.
     * @param sampleRate The current sample rate in Hz.
     * @param maxDelaySeconds Maximum expected delay time (default: 3.0s).
     */
    void PrepareToPlay(double sampleRate, float maxDelaySeconds = 3.0f);

    /**
     * @brief Sets the main delay time in seconds.
     * @param timeSeconds Delay time (0.0 to maxDelaySeconds).
     */
    void SetDelayTime(float timeSeconds);

    /**
     * @brief Sets the decay time
     * @param feedback Sets the decay time (0.0 = instant decay, 1.0 = 1 second decay)
     */
    void SetFeedbackTime(float feedback);

    /**
     * @brief Sets diffusion amount (0.0 = delay only, 1.0 = full reverb).
     * @param amount Normalized value [0.0, 1.0].
     */
    void SetDiffusionAmount(float amount);

    /**
     * @brief Sets diffusion size (scales pre-diffusion delay times).
     * @param size Normalized value [0.0, 1.0] → small to large space.
     */
    void SetDiffusionSize(float size);

    /**
     * @brief Sets diffusion quality (density and smoothness).
     * @param quality Normalized value [0.0, 1.0] → chaotic to lush.
     */
    void SetDiffusionQuality(float quality);

    /**
     * @brief Sets wet/dry mix for the effect.
     * @param mix Normalized value [0.0, 1.0] → dry to wet.
     */
    void SetDryWetMix(float mix);

    /**
     * @brief Processes an audio block.
     * @param buffer Input/output audio buffer (modified in-place).
     */
    void ProcessBlock(juce::AudioBuffer<float>& buffer);

private:
    // === Internal Helper Classes ===

    /**
     * @class AllpassDiffuser
     * @brief Single Schroeder allpass filter with variable delay.
     */
    class AllpassDiffuser
    {
    public:
        void Prepare(float sampleRate, int maxDelaySamples);
        void SetDelaySamples(int samples);
        float Process(float input);

        AllpassDiffuser() = default;

        AllpassDiffuser(AllpassDiffuser&&) noexcept = default;
        AllpassDiffuser& operator=(AllpassDiffuser&&) noexcept = default;

        AllpassDiffuser(const AllpassDiffuser&) = delete;
        AllpassDiffuser& operator=(const AllpassDiffuser&) = delete;

        // Expose buffer size for guarded clamping (internal use only)
        int getAllocatedSize() const
        {
            return buffer.getNumSamples();
        }

    private:
        juce::AudioBuffer<float> buffer;
        int writePos = 0;
        int delaySamples = 0;
        float sampleRate = 44100.0f;
    };

    /**
     * @class DiffusionStage
     * @brief Series + parallel allpass network for pre-FDN diffusion.
     */
    class DiffusionStage
    {
    public:
        void Prepare(float sampleRate, float size, float quality);
        void UpdateParameters(float size, float quality);
        float Process(float input);

    private:
        std::vector<AllpassDiffuser> seriesDiffusers;
        std::vector<AllpassDiffuser> parallelDiffusers;
        int numSeries = 2;
        int numParallel = 3;
        float currentSize = 0.5f;
        float currentQuality = 0.5f;
        float sampleRate = 44100.0f; // used for ms->samples conversions in UpdateParameters
    };

    // === Core Processing Functions ===
    void UpdateSmearAndAdvance();         // Recompute diffuser delays + advance (guarded)
    void UpdateDelayBuffer();
    void UpdateDiffusionNetwork();
    void UpdateFeedbackMatrix();
    void UpdateEchoSettings();
    void UpdateStereoMixMatrices();
    float ProcessFDNChannel(int channel, float input);

    // === Parameters ===
    float sampleRate = 44100.0f;
    float maxDelayTimeSeconds = 3.0f;

    float delayTimeSeconds = 0.5f;
    float feedbackTimeSeconds = 0.5f;
    float diffusionAmount = 0.0f;     // 0.0 = delay, 1.0 = reverb
    float diffusionSize = 0.5f;       // 0.0 = small, 1.0 = large
    float diffusionQuality = 0.5f;    // 0.0 = chaotic, 1.0 = lush
    float wetDryMix = 0.5f;           // 0.0 = dry, 1.0 = wet

    // === Buffers ===
    juce::AudioBuffer<float> delayBuffer;      // FDN delay lines (4 channels)
    juce::AudioBuffer<float> inputBuffer;      // Unused now for predelay (kept for potential future use)
    std::array<int, 4> writePos{};             // Write positions per FDN channel
    int inputWritePos = 0;

    // Dedicated echo/delay buffer (stereo)
    juce::AudioBuffer<float> echoBuffer;       // 2 channels
    std::array<int, 2> echoWritePos{};         // Write positions per echo channel
    int echoDelaySamples = 1;
    float echoFeedbackGain = 0.0f;

    // === FDN Configuration ===
    static constexpr int numFdnChannels = 4;
    std::array<int, numFdnChannels> delaySamples{};
    std::array<float, numFdnChannels> feedbackGains{};
    juce::dsp::Matrix<float> feedbackMatrix;

    // Stereo in/out mix for FDN
    std::array<float, numFdnChannels> inputMixLeft{};
    std::array<float, numFdnChannels> inputMixRight{};
    std::array<float, numFdnChannels> outputMixLeft{};
    std::array<float, numFdnChannels> outputMixRight{};

    // === Diffusion ===
    DiffusionStage diffusionStageLeft;
    DiffusionStage diffusionStageRight;

    // === Smoothing ===
    juce::SmoothedValue<float> smoothedDiffusionAmount;
    juce::SmoothedValue<float> smoothedWetDry;

    // Pre-echo smear diffusers (two per channel)
    AllpassDiffuser preEchoSmearLeftA;
    AllpassDiffuser preEchoSmearLeftB;
    AllpassDiffuser preEchoSmearRightA;
    AllpassDiffuser preEchoSmearRightB;

    // Advance scaling (max milliseconds of earlier shift applied to diffused path)
    float maxSmearAdvanceMs = 12.0f;      // Tune: 6–15 ms typical
    int currentAdvanceSamples = 0;        // Integer advance (guard / legacy)
    bool smearDiffusersReady = false;     // Set true after PrepareToPlay allocates diffusers

    // === Constants ===
    static constexpr float feedbackDecayBase = 0.5f;
    static constexpr float minDelayMs = 5.0f;
    static constexpr float maxPreDelayMs = 100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiffusedDelayReverb)
};