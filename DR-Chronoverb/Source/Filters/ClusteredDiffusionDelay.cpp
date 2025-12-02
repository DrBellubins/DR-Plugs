#include <algorithm>
#include <numeric>
#include "ClusteredDiffusionDelay.h"

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

    // Prepare diffusion chain based on a safe max per-stage delay (e.g., 50 ms)
    const int MaxStageDelaySamples = std::max(1, static_cast<int>(std::ceil(0.050f * static_cast<float>(SampleRate))));
    const int InitialStages = Diffusion::QualityToStages(stepsToNormalizedQuality(TargetDiffusionQuality.load()));
    Diffusion::Prepare(DiffusionChain, InitialStages, MaxStageDelaySamples);

    // Configure stages for current size/quality
    Diffusion::Configure(DiffusionChain,
                         SampleRate,
                         TargetDiffusionSize.load(),
                         stepsToNormalizedQuality(TargetDiffusionQuality.load()));

    // Prepare FDN with chosen number of lines and max buffer size
    FeedbackDelayNetwork::Prepare(FDNState, FDNNumberOfLines, MaxDelayBufferSamples);

    // Initialize line delays vector
    FDNLineDelaysSamples.assign(static_cast<size_t>(FDNNumberOfLines), 1.0f);

    IsPrepared = true;
}

void ClusteredDiffusionDelay::Reset()
{
    // Clear all channels' states to neutral
    for (ChannelState& State : Channels)
    {
        DelayLine::Reset(State.Delay);
        Diffusion::Reset(DiffusionChain);
        FeedbackDelayNetwork::Reset(FDNState);

        HaasStereoWidener::Reset(State.Haas);
        FeedbackDamping::Reset(State.Feedback);

        Lowpass::Reset(State.PreLP);
        Highpass::Reset(State.PreHP);

        Highpass::Reset(State.PostHP);
        Lowpass::Reset(State.PostLP);
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

    // Reconfigure stage delays for new size (quality unchanged)
    Diffusion::Configure(DiffusionChain,
                         SampleRate,
                         TargetDiffusionSize.load(),
                         stepsToNormalizedQuality(TargetDiffusionQuality.load()));
}

void ClusteredDiffusionDelay::SetDiffusionQuality(int diffusionQualitySteps)
{
    int Clamped = juce::jlimit(0, 10, diffusionQualitySteps);
    TargetDiffusionQuality.store(Clamped, std::memory_order_relaxed);

    // Reconfigure diffusion chain for new quality
    const float NewQualityNormalized = stepsToNormalizedQuality(Clamped);
    const int NewStages = Diffusion::QualityToStages(NewQualityNormalized);
    const int MaxStageDelaySamples = DiffusionChain.MaxStageDelaySamples > 0 ? DiffusionChain.MaxStageDelaySamples
                                                                            : std::max(1, static_cast<int>(std::ceil(0.050f * static_cast<float>(SampleRate))));

    Diffusion::Prepare(DiffusionChain, NewStages, MaxStageDelaySamples);
    Diffusion::Configure(DiffusionChain,
                         SampleRate,
                         TargetDiffusionSize.load(),
                         NewQualityNormalized);
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

    // --- Beat-synced delay mapping (quarter-note units) ---
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

        // APPLY mode multipliers:
        // 1 = normal (no change), 2 = triplet (* 2/3), 3 = dotted (* 1.5)
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

    // Per-sample processing
    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // Smooth time and size
        SmoothedDelayTimeSeconds = Smoothers::OnePole(SmoothedDelayTimeSeconds, MappedDelaySeconds, DelayTimeSmoothCoefficient);

        const float TargetSizeLocal = juce::jlimit(0.0f, 1.0f, TargetDiffusionSize.load(std::memory_order_relaxed));
        SmoothedDiffusionSize = Smoothers::OnePole(SmoothedDiffusionSize, TargetSizeLocal, SizeSmoothCoefficient);

        // Convert to samples
        const float BaseDelaySamples = secondsToSamples(juce::jlimit(0.0f, MaximumDelaySeconds, SmoothedDelayTimeSeconds));

        // Spread window in seconds (caps already applied); use it to derive line offsets
        const float SpreadSeconds = juce::jlimit(0.0f, MaximumSpreadSeconds, SmoothedDiffusionSize * MaximumSpreadSeconds);
        const float SpreadSamples = secondsToSamples(SpreadSeconds);

        // Map T60 to feedback gain using the nominal loop time
        const float LoopSeconds = std::max(1.0e-4f, SmoothedDelayTimeSeconds);

        const float FeedbackGainLinear = FeedbackDamping::T60ToFeedbackGain(LoopSeconds, FeedbackT60Seconds);
        FeedbackDelayNetwork::SetFeedbackGain(FDNState, FeedbackGainLinear);

        // Configure per-line delays around the base; simple symmetric offsets
        // e.g., for 4 lines: -Spread/2, -Spread/6, +Spread/6, +Spread/2
        FDNLineDelaysSamples.resize(static_cast<size_t>(FDNNumberOfLines));

        for (int LineIndex = 0; LineIndex < FDNNumberOfLines; ++LineIndex)
        {
            const float Position = (static_cast<float>(LineIndex) / static_cast<float>(std::max(1, FDNNumberOfLines - 1))) * 2.0f - 1.0f;
            const float OffsetSamples = Position * (SpreadSamples * 0.5f);
            FDNLineDelaysSamples[static_cast<size_t>(LineIndex)] = std::max(1.0f, BaseDelaySamples + OffsetSamples);
        }

        FeedbackDelayNetwork::SetLineDelays(FDNState, FDNLineDelaysSamples);

        // Compute Diffusion chain output for the bus using current dry input mix across channels
        // We will build a mono bus from the average of dry input samples (per your previous design).
        float DryInputMono = 0.0f;

        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            const float* ReadPtr = AudioBuffer.getReadPointer(ChannelIndex);
            DryInputMono += ReadPtr[SampleIndex];
        }

        if (NumChannels > 0)
            DryInputMono /= static_cast<float>(NumChannels);

        // Read current wet sum BEFORE writing new feedback (FDN convention)
        float WetSumBefore = FeedbackDelayNetwork::ReadWetSum(FDNState, FDNNormalizeWetMix);

        // Diffuse bus (equal-power controlled by DiffusionAmount)
        const float DiffusedBusSample = Diffusion::ProcessChainSample(DiffusionChain,
                                                              WetSumBefore,
                                                              DiffusionAmount,
                                                              DiffuserJitterPhase,
                                                              DiffuserJitterPhaseIncrement);

        // Dampen bus (block-wise alpha based on diffusion amount/quality)
        const float DampedBusSample = FeedbackDamping::ProcessSample(Channels[0].Feedback,
                                                             DiffusedBusSample,
                                                             DampingAlpha);

        // Ducking detector (use dry mono as detector input)
        float DuckEnvelope = Ducking::ProcessDetectorSample(Channels[0].Duck,
                                                            DryInputMono,
                                                            DuckAttackAlpha,
                                                            DuckReleaseAlpha);

        const float DuckGain = Ducking::ComputeDuckGain(DuckEnvelope, DuckAmount);

        // Apply Pre/Post HP/LP depending on mode:
        float FilteredWetOutput = WetSumBefore;

        if (UsePreFiltering)
        {
            // PRE: shape damping sample before distribution (spectral decay), duck feedback and audible wet
            float ShapedBus = Highpass::ProcessSample(Channels[0].PreHP, DampedBusSample, AlphaHP);
            ShapedBus = Lowpass::ProcessSample(Channels[0].PreLP, ShapedBus, AlphaLP);

            // Duck feedback bus and audible wet
            const float ShapedBusDucked = ShapedBus * DuckGain;
            const float AudiblyDuckedWet = WetSumBefore * DuckGain;

            // Write distributed feedback: input + feedback
            FeedbackDelayNetwork::WriteFeedbackDistributed(FDNState, ShapedBusDucked, DryInputMono);

            // Mix output for each channel (dry + wet); per-channel HP/LP not applied in PRE
            for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
            {
                float* WritePtr = AudioBuffer.getWritePointer(ChannelIndex);
                const float DrySample = WritePtr[SampleIndex];
                const float MixedSample = (DryGain * DrySample) + (WetGain * AudiblyDuckedWet);
                WritePtr[SampleIndex] = MixedSample;
            }
        }
        else
        {
            // POST: write unfiltered feedback, apply HP/LP and ducking to audible wet mix afterwards
            const float FeedbackBusDucked = DampedBusSample * DuckGain;

            FeedbackDelayNetwork::WriteFeedbackDistributed(FDNState, FeedbackBusDucked, DryInputMono);

            // Re-read wet sum (optionally could use the earlier wet; using the earlier keeps causality simple)
            float WetForOutput = WetSumBefore;

            // Apply post HP/LP to wet and then ducking to output
            float FilteredWet = Highpass::ProcessSample(Channels[0].PostHP, WetForOutput, AlphaHP);
            FilteredWet = Lowpass::ProcessSample(Channels[0].PostLP, FilteredWet, AlphaLP);

            const float AudiblyDuckedWet = FilteredWet * DuckGain;

            for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
            {
                float* WritePtr = AudioBuffer.getWritePointer(ChannelIndex);
                const float DrySample = WritePtr[SampleIndex];
                const float MixedSample = (DryGain * DrySample) + (WetGain * AudiblyDuckedWet);
                WritePtr[SampleIndex] = MixedSample;
            }
        }

        // Advance jitter phase slowly
        DiffuserJitterPhase += DiffuserJitterPhaseIncrement;

        if (DiffuserJitterPhase > juce::MathConstants<float>::twoPi)
            DiffuserJitterPhase -= juce::MathConstants<float>::twoPi;
    }
}