#include <algorithm>
#include <numeric>
#include "ClusteredDiffusionDelay.h"

// TODO: Implement fractional delay time modes

// ============================== Construction / Setup ==============================

ClusteredDiffusionDelay::ClusteredDiffusionDelay()
{
    DelayTimeSmoothCoefficient = 0.00015f; // ~10x slower, reduces jump artifacts
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
    Diffusion::TapLayout NewLayout;
    Diffusion::RecomputeTapLayout(NewLayout, stepsToNormalizedQuality(TargetDiffusionQuality.load()));

    std::atomic_store(&TapLayoutPtr, std::make_shared<Diffusion::TapLayout>(std::move(NewLayout)));

    // Allpass tap setup
    TapJitterOffsets.assign(16, 0.0f); // Support up to 16 taps (8 pairs).

    // Prepare internal all-pass delays (small micro delays: 5–13 ms region).
    const int APDelaySamples1 = static_cast<int>(std::round(0.007f * SampleRate));
    const int APDelaySamples2 = static_cast<int>(std::round(0.009f * SampleRate));
    const int APDelaySamples3 = static_cast<int>(std::round(0.011f * SampleRate));
    const int APDelaySamples4 = static_cast<int>(std::round(0.013f * SampleRate));

    for (ChannelState& State : Channels)
    {
        State.InternalAP1.prepare(APDelaySamples1);
        State.InternalAP2.prepare(APDelaySamples2);
        State.InternalAP3.prepare(APDelaySamples3);
        State.InternalAP4.prepare(APDelaySamples4);

        State.InternalAP1.setCoefficient(0.72f);
        State.InternalAP2.setCoefficient(0.70f);
        State.InternalAP3.setCoefficient(0.68f);
        State.InternalAP4.setCoefficient(0.66f);
    }

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

        Highpass::Reset(State.PostHP);
        Lowpass::Reset(State.PostLP);

        State.InternalAP1.reset();
        State.InternalAP2.reset();
        State.InternalAP3.reset();
        State.InternalAP4.reset();
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
    int clamped = juce::jlimit(0, 3, modeIndex);
    int oldMode = TargetDelayMode.load(std::memory_order_relaxed);

    if (oldMode != clamped)
    {
        TargetDelayMode.store(clamped, std::memory_order_relaxed);
        DelayModeJustChanged.store(true, std::memory_order_relaxed);
    }
}

void ClusteredDiffusionDelay::SetHostTempo(float HostTempoBPMValue)
{
    float Clamped = juce::jlimit(30.0f, 400.0f, HostTempoBPMValue);
    HostTempoBPM.store(Clamped, std::memory_order_relaxed);
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

void ClusteredDiffusionDelay::SetDiffusionQuality(int diffusionQualitySteps)
{
    int Clamped = juce::jlimit(0, 10, diffusionQualitySteps);
    TargetDiffusionQuality.store(Clamped, std::memory_order_relaxed);

    Diffusion::TapLayout NewLayout;
    Diffusion::RecomputeTapLayout(NewLayout, stepsToNormalizedQuality(Clamped));

    auto NewPtr = std::make_shared<Diffusion::TapLayout>(std::move(NewLayout));
    std::atomic_store(&TapLayoutPtr, NewPtr);
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
    TargetPreHighpassCuttoff.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetLowpassCutoff(float lpFreq)
{
    float clamped = juce::jlimit(0.0f, 1.0f, lpFreq);
    TargetPreLowpassCutoff.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetHPLPPrePost(float toggle)
{
    bool NewIsPre = (toggle > 0.5f);

    bool OldIsPre = TargetHPLPPrePost.load(std::memory_order_relaxed);

    if (OldIsPre != NewIsPre)
    {
        TargetHPLPPrePost.store(NewIsPre, std::memory_order_relaxed);

        // Hygiene: reset both sets of filter states to avoid residual ramps when mode changes.
        for (ChannelState& State : Channels)
        {
            Highpass::Reset(State.PreHP);
            Lowpass::Reset(State.PreLP);

            Highpass::Reset(State.PostHP);
            Lowpass::Reset(State.PostLP);
        }
    }
}

// Ducking
void ClusteredDiffusionDelay::SetDuckAmount(float duckAmount)
{
    float clamped = juce::jlimit(0.0f, 1.0f, duckAmount);
    TargetDuckAmount.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDuckAttack(float duckAttack)
{
    float clamped = juce::jlimit(0.0f, 1.0f, duckAttack);
    TargetDuckAttack.store(clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDuckRelease(float duckRelease)
{
    float clamped = juce::jlimit(0.0f, 1.0f, duckRelease);
    TargetDuckRelease.store(clamped, std::memory_order_relaxed);
}

// ============================== Processing ==============================
void ClusteredDiffusionDelay::ProcessBlock(juce::AudioBuffer<float>& AudioBuffer)
{
    if (!IsPrepared)
        return;

    auto LocalTapLayoutPtr = std::atomic_load(&TapLayoutPtr);

    if (!LocalTapLayoutPtr)
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

            Ducking::Reset(Channels[ChannelIndex].Duck);
        }
    }

    // Cache parameters for the block
    const float DiffusionAmount = TargetDiffusionAmount.load(std::memory_order_relaxed);

    const int DiffusionQuality = TargetDiffusionQuality.load(std::memory_order_relaxed);
    const float DiffusionQualityNormalized = stepsToNormalizedQuality(DiffusionQuality);
    const int QualityTier = computeQualityTier(DiffusionQualityNormalized);

    const float FeedbackT60Seconds = TargetFeedbackTimeSeconds.load(std::memory_order_relaxed);

    const float DryWetMix = TargetDryWetMix.load(std::memory_order_relaxed);
    float DryGain = 1.0f;
    float WetGain = 0.0f;

    DryWetMixer::ComputeGains(DryWetMix, DryGain, WetGain);

    const float StereoWidth = TargetStereoWidth.load(std::memory_order_relaxed);

    const float HPDecayAmount = TargetPreHighpassCuttoff.load(std::memory_order_relaxed);
    const float LPDecayAmount = TargetPreLowpassCutoff.load(std::memory_order_relaxed);

    const bool UsePreFiltering = TargetHPLPPrePost.load(std::memory_order_relaxed);

    // Compute pre-filter coefficients for this block (decouples CPU from per-sample mapping)
    const float AlphaHP = Highpass::AmountToAlpha(static_cast<float>(SampleRate), HPDecayAmount);
    const float AlphaLP = Lowpass::AmountToAlpha(static_cast<float>(SampleRate), LPDecayAmount);

    // Equal-power crossfade amounts for diffusion blend (0 => base tap, 1 => cluster)
    const float AmountA = std::cos(DiffusionAmount * juce::MathConstants<float>::halfPi);
    const float AmountB = std::sin(DiffusionAmount * juce::MathConstants<float>::halfPi);

    // Damping alpha for feedback path (depends on amount and quality)
    const float DampingAlpha = FeedbackDamping::ComputeDampingAlpha(static_cast<float>(SampleRate),
                                                                    DiffusionAmount,
                                                                    DiffusionQualityNormalized);

    // Prepare spread constants for negative-offset lookahead
    const float MaxSpreadSamples = secondsToSamples(MaximumSpreadSeconds);
    const float LookaheadSamples = 0.5f * MaxSpreadSamples;

    // Fetch duck values and compute alphas once per block
    const float DuckAmount  = TargetDuckAmount.load(std::memory_order_relaxed);
    const float DuckAttackN = TargetDuckAttack.load(std::memory_order_relaxed);
    const float DuckReleaseN = TargetDuckRelease.load(std::memory_order_relaxed);

    float DuckAttackAlpha = 0.0f;
    float DuckReleaseAlpha = 0.0f;

    Ducking::ComputeAttackReleaseAlphas(SampleRate,
                                        DuckAttackN,
                                        DuckReleaseN,
                                        DuckAttackAlpha,
                                        DuckReleaseAlpha);

    // Delay time
    const int CurrentMode = TargetDelayMode.load(std::memory_order_relaxed);
    const float RawDelayParam = TargetDelayTimeSeconds.load(std::memory_order_relaxed); // Interpreted by mode
    const float CurrentBPM = HostTempoBPM.load(std::memory_order_relaxed);


    // Table entries expressed in QUARTER-NOTE units ("beats"):
    // Whole = 4.0, Half = 2.0, Quarter = 1.0, Eighth = 0.5, Sixteenth = 0.25
    static const float BeatFractions[] =
    {
        4.0f,   // Whole
        2.0f,   // Half
        1.0f,   // Quarter
        0.5f,   // Eighth
        0.25f   // Sixteenth
    };

    const int FractionCount = static_cast<int>(sizeof(BeatFractions) / sizeof(float));

    float MappedDelaySeconds = RawDelayParam; // Default: ms mode uses raw as seconds (0..MaximumDelaySeconds).

    if (CurrentMode != 0) // Not ms mode: interpret RawDelayParam as a selector.
    {
        const float ClampedFraction = juce::jlimit(0.0f, 1.0f, RawDelayParam);

        int FractionIndex = static_cast<int>(std::round(ClampedFraction * static_cast<float>(FractionCount - 1)));
        FractionIndex = juce::jlimit(0, FractionCount - 1, FractionIndex);

        float Beats = BeatFractions[FractionIndex];

        // APPLY mode multipliers: 1 = normal (no change), 2 = triplet (* 2/3), 3 = dotted (* 1.5)
        if (CurrentMode == 2)
            Beats *= (2.0f / 3.0f);
        else if (CurrentMode == 3)
            Beats *= 1.5f;

        const float SecondsPerQuarter = (CurrentBPM > 0.0f ? 60.0f / CurrentBPM : 0.5f); // Fallback to 120 BPM
        MappedDelaySeconds = juce::jlimit(0.0f, MaximumDelaySeconds, Beats * SecondsPerQuarter);
    }

    // Instead: just clear the flag; smoothing will glide.
    if (DelayModeJustChanged.load(std::memory_order_relaxed))
        DelayModeJustChanged.store(false, std::memory_order_relaxed);

    // Adaptive spread
    const float AdaptiveSpreadSeconds = computeAdaptiveSpreadSeconds(DiffusionQualityNormalized);

    // Jitter range (normalized offset domain): grows with quality tier (only tier >=1 noticeable).
    float JitterRange = 0.0f;
    if (QualityTier == 1)
        JitterRange = 0.010f; // ±0.01
    else if (QualityTier == 2)
        JitterRange = 0.020f; // ±0.02

    // Refresh jitter offsets once per block if > 0.
    if (JitterRange > 0.0f)
    {
        for (size_t J = 0; J < TapJitterOffsets.size(); ++J)
        {
            float r = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
            TapJitterOffsets[J] = (r * 2.0f - 1.0f) * JitterRange;
        }
    }
    else
        std::fill(TapJitterOffsets.begin(), TapJitterOffsets.end(), 0.0f);

    // Per-sample processing
    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        SmoothedDelayTimeSeconds = Smoothers::OnePole(SmoothedDelayTimeSeconds, MappedDelaySeconds, DelayTimeSmoothCoefficient);

        const float TargetSizeLocal = juce::jlimit(0.0f, 1.0f, TargetDiffusionSize.load(std::memory_order_relaxed));
        SmoothedDiffusionSize = Smoothers::OnePole(SmoothedDiffusionSize, TargetSizeLocal, SizeSmoothCoefficient);

        // Convert to samples (guard spread by derived maximum)
        const float BaseDelaySamples = secondsToSamples(juce::jlimit(0.0f, MaximumDelaySeconds, SmoothedDelayTimeSeconds));

        const float RawSpreadSeconds = juce::jlimit(0.0f, AdaptiveSpreadSeconds, SmoothedDiffusionSize * AdaptiveSpreadSeconds);
        const float SpreadSamples = secondsToSamples(RawSpreadSeconds);

        // Map T60 to per-loop feedback gain using the current loop time (nominal delay)
        const float LoopSeconds = std::max(1.0e-4f, SmoothedDelayTimeSeconds);
        const float FeedbackGain = FeedbackDamping::T60ToFeedbackGain(LoopSeconds, FeedbackT60Seconds);

        // Compute the wet echo for each channel (pre-stereo stage)
        float WetEchoLeft = 0.0f;
        float WetEchoRight = 0.0f;
        float BaseTapLeft = 0.0f;
        float ClusterLeft = 0.0f;
        float BaseTapRight = 0.0f;
        float ClusterRight = 0.0f;

        // Channel 0
        if (NumChannels >= 1)
        {
            Diffusion::ComputeWetEcho(Channels[0].Delay,
                                      BaseDelaySamples,
                                      SpreadSamples,
                                      LookaheadSamples,
                                      *LocalTapLayoutPtr,
                                      AmountA,
                                      AmountB,
                                      TapJitterOffsets,
                                      BaseTapLeft,
                                      ClusterLeft);
        }

        // Channel 1
        if (NumChannels >= 2)
        {
            Diffusion::ComputeWetEcho(Channels[1].Delay,
                                      BaseDelaySamples,
                                      SpreadSamples,
                                      LookaheadSamples,
                                      *LocalTapLayoutPtr,
                                      AmountA,
                                      AmountB,
                                      TapJitterOffsets,
                                      BaseTapRight,
                                      ClusterRight);
        }

        // Stereo widening/reduction stage
        float ProcessedWetLeft = (AmountA * BaseTapLeft) + (AmountB * ClusterLeft);
        float ProcessedWetRight = (AmountA * BaseTapRight) + (AmountB * ClusterRight);

        if (NumChannels >= 2)
        {
            // Feed the computed wet into Haas, receive widened/reduced outputs
            HaasStereoWidener::ProcessStereoSample(ProcessedWetLeft,
                                                   ProcessedWetRight,
                                                   StereoWidth,
                                                   Channels[0].Haas,
                                                   Channels[1].Haas,
                                                   WetEchoLeft,
                                                   WetEchoRight);

            // Continue processing with the Haas outputs
            ProcessedWetLeft = WetEchoLeft;
            ProcessedWetRight = WetEchoRight;
        }
        else if (NumChannels == 1)
        {
            // Mono path: write the processed wet into Haas to keep buffer consistent, pass-through value
            HaasStereoWidener::WriteWet(Channels[0].Haas, ProcessedWetLeft);
            HaasStereoWidener::Advance(Channels[0].Haas);
            // Keep ProcessedWetLeft as-is (no stereo transform in mono)
        }

        // Per-channel feedback, pre/post-filtering, delay write, and output mixing
        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            float* ChannelData = AudioBuffer.getWritePointer(ChannelIndex);
            ChannelState& State = Channels[ChannelIndex];

            const float InputSample = ChannelData[SampleIndex];

            const float ClusterInjectionBlend = (QualityTier == 2
                                     ? juce::jlimit(0.0f, 1.0f, (DiffusionQualityNormalized - 0.65f) / 0.35f) // smoothstep-ish
                                     : 0.0f);

            // Use injected sample instead of WetSampleUnfiltered for damping filter input (recursive diffusion seed).
            float WetSampleUnfiltered = (ChannelIndex == 0 ? ProcessedWetLeft : ProcessedWetRight);

            // Extract cluster-only and base-only components for more precise injection (optional subtlety):
            float BaseTapSample = (ChannelIndex == 0 ? BaseTapLeft : BaseTapRight);
            float ClusterSample = (ChannelIndex == 0 ? ClusterLeft : ClusterRight);

            // Blend for feedback seed (preserve character at low quality):
            float FeedbackSeed = (1.0f - ClusterInjectionBlend) * BaseTapSample
                                 + ClusterInjectionBlend * ClusterSample;

            // Internal all-pass recursive diffusion (tier >=1) BEFORE damping:
            if (QualityTier >= 1)
            {
                ChannelState& TierState = State;
                FeedbackSeed = TierState.InternalAP1.processSample(FeedbackSeed);
                FeedbackSeed = TierState.InternalAP2.processSample(FeedbackSeed);

                if (QualityTier == 2)
                {
                    FeedbackSeed = TierState.InternalAP3.processSample(FeedbackSeed);
                    FeedbackSeed = TierState.InternalAP4.processSample(FeedbackSeed);
                }
            }

            // Now run damping on FeedbackSeed instead of WetSampleUnfiltered:
            const float FeedbackSamplePreFilters = FeedbackDamping::ProcessSample(State.Feedback,
                                                                                  FeedbackSeed,
                                                                                  DampingAlpha,
                                                                                  FeedbackGain);

            // --- Ducking detector and gain computation (per channel) ---
            float DuckEnvelope = Ducking::ProcessDetectorSample(State.Duck,
                                                                InputSample,
                                                                DuckAttackAlpha,
                                                                DuckReleaseAlpha);

            float DuckGain = Ducking::ComputeDuckGain(DuckEnvelope, DuckAmount);

            const float FeedbackWithDucking = FeedbackSamplePreFilters * DuckGain;
            float DelayLineInput = 0.0f;
            float OutputWetSample = WetSampleUnfiltered;

            if (UsePreFiltering)
            {
                // PRE mode: HP/LP shape feedback -> spectral decay
                float ShapedFeedback = Highpass::ProcessSample(State.PreHP,
                                                               FeedbackWithDucking,
                                                               AlphaHP);
                ShapedFeedback = Lowpass::ProcessSample(State.PreLP,
                                                        ShapedFeedback,
                                                        AlphaLP);

                DelayLineInput = InputSample + ShapedFeedback;

                // Apply ducking to audible wet output (unfiltered in PRE mode)
                OutputWetSample = WetSampleUnfiltered * DuckGain;
            }
            else
            {
                // POST mode: Write unfiltered feedback (no spectral decay);
                // apply HP/LP only to final wet output for static coloration.
                DelayLineInput = InputSample + FeedbackWithDucking;

                float FilteredWet = Highpass::ProcessSample(State.PostHP,
                                                            WetSampleUnfiltered,
                                                            AlphaHP);
                FilteredWet = Lowpass::ProcessSample(State.PostLP,
                                                     FilteredWet,
                                                     AlphaLP);

                // Apply ducking after filtering in POST mode
                OutputWetSample = FilteredWet * DuckGain;
            }

            // Write to delay line
            DelayLine::Write(State.Delay, DelayLineInput);

            // Final dry/wet mix (use OutputWetSample which may be post-filtered)
            const float Mixed = (DryGain * InputSample) + (WetGain * OutputWetSample);
            ChannelData[SampleIndex] = Mixed;
        }
    }
}