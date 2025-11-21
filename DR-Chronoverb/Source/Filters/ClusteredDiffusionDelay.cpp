#include <algorithm>
#include <numeric>
#include "ClusteredDiffusionDelay.h"

// ============================== Construction / Setup ==============================

ClusteredDiffusionDelay::ClusteredDiffusionDelay()
{
}

ClusteredDiffusionDelay::~ClusteredDiffusionDelay()
{
}

void ClusteredDiffusionDelay::PrepareToPlay(double NewSampleRate, float NewMaximumDelaySeconds)
{
    // Store sample rate and constraints (guard against invalid rates)
    SampleRate = (NewSampleRate > 0.0 ? NewSampleRate : 44100.0);
    MaximumDelaySeconds = std::max(0.001f, NewMaximumDelaySeconds);

    // Haas setup (max ~40 ms)
    const float HaasMaxMs = 40.0f;
    HaasMaxDelaySamples = std::max(1, static_cast<int>(std::ceil((HaasMaxMs / 1000.0f) * static_cast<float>(SampleRate))));

    // Derive maximum spread as a fraction of maximum delay (cap to 150 ms)
    MaximumSpreadSeconds = std::min(0.150f, 0.25f * MaximumDelaySeconds);

    // Compute maximum required delay buffer length:
    // nominal delay + full spread + look-ahead + safety margin
    const float SafetySeconds = 0.020f;
    const float MaxTotalSeconds = MaximumDelaySeconds + MaximumSpreadSeconds + (0.5f * MaximumSpreadSeconds) + SafetySeconds;

    MaxDelayBufferSamples = std::max(1, static_cast<int>(std::ceil(MaxTotalSeconds * static_cast<float>(SampleRate))));

    // Allocate/initialize channel states
    Channels.clear();

    // Initialize smoothed parameters to targets to avoid startup glides
    SmoothedDelayTimeSeconds = TargetDelayTimeSeconds.load();
    SmoothedDiffusionSize    = TargetDiffusionSize.load();

    // Compute initial tap layout from default quality
    Diffusion::RecomputeTapLayout(TapLayout, TargetDiffusionQuality.load());

    IsPrepared = true;
}

void ClusteredDiffusionDelay::Reset()
{
    // Clear all channels' states to neutral
    for (ChannelState& State : Channels)
    {
        DelayLine::Reset(State.Delay);
        HaasStereoWidener::Reset(State.Haas);
        FeedbackDamping::Reset(State.Feedback);
        Lowpass::Reset(State.PreLP);
        Highpass::Reset(State.PreHP);
    }
}

// ============================== Parameter Setters ==============================
void ClusteredDiffusionDelay::SetDelayTime(float delayTimeSeconds)
{
    // Clamp to configured maximum delay
    float clamped = juce::jlimit(0.0f, MaximumDelaySeconds, delayTimeSeconds);
    TargetDelayTimeSeconds.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDelayMode(int modeIndex)
{
    int clamped = juce::jlimit(0, 4, modeIndex);
}

void ClusteredDiffusionDelay::SetFeedbackTime(float feedbackTimeSeconds)
{
    // Clamp to [0..10] seconds T60 (0 disables feedback)
    float clamped = juce::jlimit(0.0f, 10.0f, feedbackTimeSeconds);
    TargetFeedbackTimeSeconds.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDiffusionAmount(float diffusionAmount)
{
    float clamped = juce::jlimit(0.0f, 1.0f, diffusionAmount);
    TargetDiffusionAmount.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDiffusionSize(float diffusionSize)
{
    float clamped = juce::jlimit(0.0f, 1.0f, diffusionSize);
    TargetDiffusionSize.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDiffusionQuality(float diffusionQuality)
{
    float clamped = juce::jlimit(0.0f, 1.0f, diffusionQuality);
    TargetDiffusionQuality.store(clamped, std::memory_order_relaxed);

    // Update tap layout immediately when quality changes (affects density)
    Diffusion::RecomputeTapLayout(TapLayout, clamped);
}

void ClusteredDiffusionDelay::SetDryWetMix(float dryWet)
{
    float clamped = juce::jlimit(0.0f, 1.0f, dryWet);
    TargetDryWetMix.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetStereoSpread(float stereoWidth)
{
    float clamped = juce::jlimit(-1.0f, 1.0f, stereoWidth);
    TargetStereoWidth.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetHighpassCutoff(float hpFreq)
{
    float clamped = juce::jlimit(0.0f, 1.0f, hpFreq);
    TargetPreHighpassDecayAmount.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetLowpassCutoff(float lpFreq)
{
    float clamped = juce::jlimit(0.0f, 1.0f, lpFreq);
    TargetPreLowpassDecayAmount.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetHPLPPrePost(float toggle)
{
    TargetHPLPPrePost = toggle > 0.5f ? true : false;
}

// ============================== Processing ==============================
void ClusteredDiffusionDelay::ProcessBlock(juce::AudioBuffer<float>& AudioBuffer)
{
    if (!IsPrepared)
        return;

    const int NumChannels = AudioBuffer.getNumChannels();
    const int NumSamples = AudioBuffer.getNumSamples();

    // Ensure per-channel states exist and are prepared (allocate buffers if needed)
    if (static_cast<int>(Channels.size()) < NumChannels)
    {
        int OldSize = static_cast<int>(Channels.size());
        Channels.resize(NumChannels);

        for (int ChannelIndex = OldSize; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            // Prepare delay buffer
            DelayLine::Prepare(Channels[ChannelIndex].Delay, MaxDelayBufferSamples);

            // Prepare Haas buffer
            HaasStereoWidener::Prepare(Channels[ChannelIndex].Haas, HaasMaxDelaySamples);

            // Reset the rest to neutral
            FeedbackDamping::Reset(Channels[ChannelIndex].Feedback);
            Highpass::Reset(Channels[ChannelIndex].PreHP);
            Lowpass::Reset(Channels[ChannelIndex].PreLP);
        }
    }

    // Cache parameters for the block
    const float DiffusionAmount = TargetDiffusionAmount.load(std::memory_order_relaxed);
    const float DiffusionQuality = TargetDiffusionQuality.load(std::memory_order_relaxed);
    const float FeedbackT60Seconds = TargetFeedbackTimeSeconds.load(std::memory_order_relaxed);

    const float DryWetMix = TargetDryWetMix.load(std::memory_order_relaxed);
    float DryGain = 1.0f;
    float WetGain = 0.0f;

    DryWetMixer::ComputeGains(DryWetMix, DryGain, WetGain);

    const float StereoWidth = TargetStereoWidth.load(std::memory_order_relaxed);

    const float HPDecayAmount = TargetPreHighpassDecayAmount.load(std::memory_order_relaxed);
    const float LPDecayAmount = TargetPreLowpassDecayAmount.load(std::memory_order_relaxed);

    // Compute pre-filter coefficients for this block (decouples CPU from per-sample mapping)
    const float AlphaHP = Highpass::AmountToAlpha(static_cast<float>(SampleRate), HPDecayAmount);
    const float AlphaLP = Lowpass::AmountToAlpha(static_cast<float>(SampleRate), LPDecayAmount);

    // Equal-power crossfade amounts for diffusion blend (0 => base tap, 1 => cluster)
    const float AmountA = std::cos(DiffusionAmount * juce::MathConstants<float>::halfPi);
    const float AmountB = std::sin(DiffusionAmount * juce::MathConstants<float>::halfPi);

    // Damping alpha for feedback path (depends on amount and quality)
    const float DampingAlpha = FeedbackDamping::ComputeDampingAlpha(static_cast<float>(SampleRate),
                                                                    DiffusionAmount,
                                                                    DiffusionQuality);

    // Prepare spread constants for negative-offset lookahead
    const float MaxSpreadSamples = secondsToSamples(MaximumSpreadSeconds);
    const float LookaheadSamples = 0.5f * MaxSpreadSamples;

    // Per-sample processing
    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // Smooth time-varying parameters (delay time and spread size)
        const float TargetDelaySecondsLocal = juce::jlimit(0.0f, MaximumDelaySeconds, TargetDelayTimeSeconds.load(std::memory_order_relaxed));
        const float TargetSizeLocal = juce::jlimit(0.0f, 1.0f, TargetDiffusionSize.load(std::memory_order_relaxed));

        SmoothedDelayTimeSeconds = Smoothers::OnePole(SmoothedDelayTimeSeconds, TargetDelaySecondsLocal, DelayTimeSmoothCoefficient);
        SmoothedDiffusionSize    = Smoothers::OnePole(SmoothedDiffusionSize,    TargetSizeLocal,       SizeSmoothCoefficient);

        // Convert to samples (guard spread by derived maximum)
        const float BaseDelaySamples = secondsToSamples(juce::jlimit(0.0f, MaximumDelaySeconds, SmoothedDelayTimeSeconds));
        const float SpreadSamples    = secondsToSamples(juce::jlimit(0.0f, MaximumSpreadSeconds, SmoothedDiffusionSize * MaximumSpreadSeconds));

        // Map T60 to per-loop feedback gain using the current loop time (nominal delay)
        const float LoopSeconds = std::max(1.0e-4f, SmoothedDelayTimeSeconds);
        const float FeedbackGain = FeedbackDamping::T60ToFeedbackGain(LoopSeconds, FeedbackT60Seconds);

        // Compute the wet echo for each channel (pre-stereo stage)
        float WetEchoLeft = 0.0f;
        float WetEchoRight = 0.0f;

        // Channel 0
        if (NumChannels >= 1)
        {
            WetEchoLeft = Diffusion::ComputeWetEcho(Channels[0].Delay,
                                                    BaseDelaySamples,
                                                    SpreadSamples,
                                                    LookaheadSamples,
                                                    TapLayout,
                                                    AmountA,
                                                    AmountB);
        }

        // Channel 1
        if (NumChannels >= 2)
        {
            WetEchoRight = Diffusion::ComputeWetEcho(Channels[1].Delay,
                                                     BaseDelaySamples,
                                                     SpreadSamples,
                                                     LookaheadSamples,
                                                     TapLayout,
                                                     AmountA,
                                                     AmountB);
        }

        // Stereo widening/reduction stage
        float ProcessedWetLeft = WetEchoLeft;
        float ProcessedWetRight = WetEchoRight;

        if (NumChannels >= 2)
        {
            HaasStereoWidener::ProcessStereoSample(WetEchoLeft,
                                                   WetEchoRight,
                                                   StereoWidth,
                                                   Channels[0].Haas,
                                                   Channels[1].Haas,
                                                   ProcessedWetLeft,
                                                   ProcessedWetRight);
        }
        else if (NumChannels == 1)
        {
            // Mono path: write/advance Haas to keep consistent, pass-through value
            HaasStereoWidener::WriteWet(Channels[0].Haas, WetEchoLeft);
            HaasStereoWidener::Advance(Channels[0].Haas);

            ProcessedWetLeft = WetEchoLeft;
        }

        // Per-channel feedback, pre-filtering, delay write, and output mixing
        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            float* ChannelData = AudioBuffer.getWritePointer(ChannelIndex);
            ClusteredDiffusionDelay::ChannelState& State = Channels[ChannelIndex];

            const float InputSample = ChannelData[SampleIndex];
            const float WetSample = (ChannelIndex == 0 ? ProcessedWetLeft : ProcessedWetRight);

            // Feedback damping (LPF) and gain
            const float FeedbackSamplePreFilters = FeedbackDamping::ProcessSample(State.Feedback,
                                                                                  WetSample,
                                                                                  DampingAlpha,
                                                                                  FeedbackGain);

            // Pre high-pass, then pre low-pass shaping for natural decay
            float ShapedFeedback = Highpass::ProcessSample(State.PreHP, FeedbackSamplePreFilters, AlphaHP);
            ShapedFeedback = Lowpass::ProcessSample(State.PreLP, ShapedFeedback, AlphaLP);

            // Write input + shaped feedback into delay line
            const float DelayLineInput = InputSample + ShapedFeedback;
            DelayLine::Write(State.Delay, DelayLineInput);

            // Final dry/wet mix to output
            const float Mixed = (DryGain * InputSample) + (WetGain * WetSample);
            ChannelData[SampleIndex] = Mixed;
        }
    }
}