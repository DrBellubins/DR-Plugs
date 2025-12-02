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
    SampleRate = (NewSampleRate > 0.0 ? NewSampleRate : 44100.0);
    MaximumDelaySeconds = std::max(0.001f, NewMaximumDelaySeconds);

    const float HaasMaxMs = 40.0f;
    HaasMaxDelaySamples = std::max(1, static_cast<int>(std::ceil((HaasMaxMs / 1000.0f) * static_cast<float>(SampleRate))));

    // Pre-delay buffer: fixed 1000 ms capacity
    PreDelayBufferSamples = std::max(1, static_cast<int>(std::ceil(1.000f * static_cast<float>(SampleRate))));

    // Keep a modest safety for legacy delay buffers (no longer used for main read)
    MaxDelayBufferSamples = std::max(PreDelayBufferSamples, static_cast<int>(std::ceil((MaximumDelaySeconds + 0.020f) * static_cast<float>(SampleRate))));

    Channels.clear();

    SmoothedDelayTimeSeconds = TargetDelayTimeSeconds.load();
    SmoothedDiffusionSize    = TargetDiffusionSize.load();

    const int MaxStageDelaySamples = std::max(1, static_cast<int>(std::ceil(0.050f * static_cast<float>(SampleRate))));
    const int InitialStages = Diffusion::QualityToStages(stepsToNormalizedQuality(TargetDiffusionQuality.load()));
    Diffusion::Prepare(DiffusionChain, InitialStages, MaxStageDelaySamples);

    Diffusion::Configure(DiffusionChain,
                         SampleRate,
                         TargetDiffusionSize.load(),
                         stepsToNormalizedQuality(TargetDiffusionQuality.load()));

    FeedbackDelayNetwork::Prepare(FDNState, FDNNumberOfLines, MaxDelayBufferSamples);

    FDNLineDelaysSamples.assign(static_cast<size_t>(FDNNumberOfLines), 1.0f);

    // Nominal short loop used for feedback gain mapping
    NominalFDNLoopSeconds = 0.050f;

    IsPrepared = true;
}

void ClusteredDiffusionDelay::Reset()
{
    // Clear all channels' states to neutral
    for (ChannelState& State : Channels)
    {
        DelayLine::Reset(State.Delay);
        DelayLine::Reset(State.PreDelay);

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

    // Ensure per-channel states exist and are prepared
    if (static_cast<int>(Channels.size()) < NumChannels)
    {
        int OldSize = static_cast<int>(Channels.size());
        Channels.resize(NumChannels);

        for (int ChannelIndex = OldSize; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            DelayLine::Prepare(Channels[ChannelIndex].PreDelay, PreDelayBufferSamples); // NEW
            DelayLine::Prepare(Channels[ChannelIndex].Delay, MaxDelayBufferSamples);

            HaasStereoWidener::Prepare(Channels[ChannelIndex].Haas, HaasMaxDelaySamples);

            FeedbackDamping::Reset(Channels[ChannelIndex].Feedback);
            Highpass::Reset(Channels[ChannelIndex].PreHP);
            Lowpass::Reset(Channels[ChannelIndex].PreLP);
            Highpass::Reset(Channels[ChannelIndex].PostHP);
            Lowpass::Reset(Channels[ChannelIndex].PostLP);

            Ducking::Reset(Channels[ChannelIndex].Duck);
        }
    }

    const float DiffusionAmount = TargetDiffusionAmount.load(std::memory_order_relaxed);
    const int DiffusionQualitySteps = TargetDiffusionQuality.load(std::memory_order_relaxed);
    const float DiffusionQualityNormalized = stepsToNormalizedQuality(DiffusionQualitySteps);

    const float FeedbackT60Seconds = TargetFeedbackTimeSeconds.load(std::memory_order_relaxed);

    const float DryWetMix = TargetDryWetMix.load(std::memory_order_relaxed);
    float DryGain = 1.0f;
    float WetGain = 0.0f;
    DryWetMixer::ComputeGains(DryWetMix, DryGain, WetGain);

    const float StereoWidth = TargetStereoWidth.load(std::memory_order_relaxed);

    const float HPDecayAmount = TargetPreHighpassCuttoff.load(std::memory_order_relaxed);
    const float LPDecayAmount = TargetPreLowpassCutoff.load(std::memory_order_relaxed);
    const bool UsePreFiltering = TargetHPLPPrePost.load(std::memory_order_relaxed);

    const float AlphaHP = Highpass::AmountToAlpha(static_cast<float>(SampleRate), HPDecayAmount);
    const float AlphaLP = Lowpass::AmountToAlpha(static_cast<float>(SampleRate), LPDecayAmount);

    const float AmountA = std::cos(DiffusionAmount * juce::MathConstants<float>::halfPi);
    const float AmountB = std::sin(DiffusionAmount * juce::MathConstants<float>::halfPi);

    const float DampingAlpha = FeedbackDamping::ComputeDampingAlpha(static_cast<float>(SampleRate),
                                                                    DiffusionAmount,
                                                                    DiffusionQualityNormalized);

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

    // --- Delay time mapping now drives PRE-DELAY only ---
    const int CurrentMode = TargetDelayMode.load(std::memory_order_relaxed);
    const float RawDelayParam = TargetDelayTimeSeconds.load(std::memory_order_relaxed);
    const float CurrentBPM = HostTempoBPM.load(std::memory_order_relaxed);

    static const float BeatFractions[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    const int FractionCount = static_cast<int>(sizeof(BeatFractions) / sizeof(float));

    float PreDelaySeconds = RawDelayParam;

    if (CurrentMode != 0)
    {
        const float ClampedFraction = juce::jlimit(0.0f, 1.0f, RawDelayParam);
        int FractionIndex = static_cast<int>(std::round(ClampedFraction * static_cast<float>(FractionCount - 1)));
        FractionIndex = juce::jlimit(0, FractionCount - 1, FractionIndex);

        float Beats = BeatFractions[FractionIndex];

        if (CurrentMode == 2)
            Beats *= (2.0f / 3.0f);
        else if (CurrentMode == 3)
            Beats *= 1.5f;

        const float SecondsPerQuarter = (CurrentBPM > 0.0f ? 60.0f / CurrentBPM : 0.5f);
        PreDelaySeconds = juce::jlimit(0.0f, 1.000f, Beats * SecondsPerQuarter);
    }

    if (DelayModeJustChanged.load(std::memory_order_relaxed))
        DelayModeJustChanged.store(false, std::memory_order_relaxed);

    // --- Configure short FDN delays (ignoring main delayTime) ---
    // Base primes in milliseconds: ~23, 31, 47, 61 ms; scaled by diffusionSize in [0.5 .. 2.0]
    const float SizeClamped = juce::jlimit(0.0f, 1.0f, TargetDiffusionSize.load(std::memory_order_relaxed));
    const float SizeScale = juce::jmap(SizeClamped, 0.0f, 1.0f, 0.5f, 2.0f);

    static const float BaseMsFDN[] = { 23.0f, 31.0f, 47.0f, 61.0f };
    FDNLineDelaysSamples.resize(static_cast<size_t>(FDNNumberOfLines));

    float LoopSecondsAccum = 0.0f;

    for (int LineIndex = 0; LineIndex < FDNNumberOfLines; ++LineIndex)
    {
        const float BaseMs = BaseMsFDN[LineIndex % static_cast<int>(sizeof(BaseMsFDN) / sizeof(float))];
        const float StageMs = BaseMs * SizeScale;

        const float DelaySamples = static_cast<float>(std::round((StageMs / 1000.0f) * static_cast<float>(SampleRate)));
        FDNLineDelaysSamples[static_cast<size_t>(LineIndex)] = std::max(1.0f, DelaySamples);

        LoopSecondsAccum += (StageMs / 1000.0f);
    }

    // Nominal loop time = average of short delays
    NominalFDNLoopSeconds = std::max(0.005f, LoopSecondsAccum / static_cast<float>(FDNNumberOfLines));

    FeedbackDelayNetwork::SetLineDelays(FDNState, FDNLineDelaysSamples);

    const float FeedbackGainLinear = FeedbackDamping::T60ToFeedbackGain(NominalFDNLoopSeconds, FeedbackT60Seconds);
    FeedbackDelayNetwork::SetFeedbackGain(FDNState, FeedbackGainLinear);

    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // Smooth pre-delay time
        SmoothedDelayTimeSeconds = Smoothers::OnePole(SmoothedDelayTimeSeconds, PreDelaySeconds, DelayTimeSmoothCoefficient);

        const float PreDelaySamples = secondsToSamples(juce::jlimit(0.0f, 1.000f, SmoothedDelayTimeSeconds));

        // Build dry mono detector
        float DryInputMono = 0.0f;

        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            const float* ReadPtr = AudioBuffer.getReadPointer(ChannelIndex);
            DryInputMono += ReadPtr[SampleIndex];
        }

        if (NumChannels > 0)
            DryInputMono /= static_cast<float>(NumChannels);

        // --- PRE-DELAY WRITE/READ (clean delay path) ---
        float PreDelayedSample = DryInputMono;

        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
            DelayLine::Write(Channels[ChannelIndex].PreDelay, AudioBuffer.getReadPointer(ChannelIndex)[SampleIndex]);

        PreDelayedSample = DelayLine::Read(Channels[0].PreDelay, PreDelaySamples);

        // --- FDN wet sum BEFORE writing new feedback ---
        float WetSumBefore = FeedbackDelayNetwork::ReadWetSum(FDNState, FDNNormalizeWetMix);

        // Diffuse bus based on current wet sum (keeps tail smooth)
        const float DiffusedBusSample = Diffusion::ProcessChainSample(DiffusionChain,
                                                                      WetSumBefore,
                                                                      DiffusionAmount,
                                                                      DiffuserJitterPhase,
                                                                      DiffuserJitterPhaseIncrement);

        // Dampen bus
        const float DampedBusSample = FeedbackDamping::ProcessSample(Channels[0].Feedback,
                                                                     DiffusedBusSample,
                                                                     DampingAlpha);

        // Ducking envelope from dry input
        float DuckEnvelope = Ducking::ProcessDetectorSample(Channels[0].Duck,
                                                            DryInputMono,
                                                            DuckAttackAlpha,
                                                            DuckReleaseAlpha);

        const float DuckGain = Ducking::ComputeDuckGain(DuckEnvelope, DuckAmount);

        // --- PRE/POST FILTERING FOR FEEDBACK BUS (tone only) ---
        float ShapedBusForTone = DampedBusSample;

        if (UsePreFiltering)
        {
            ShapedBusForTone = Highpass::ProcessSample(Channels[0].PreHP, ShapedBusForTone, AlphaHP);
            ShapedBusForTone = Lowpass::ProcessSample(Channels[0].PreLP, ShapedBusForTone, AlphaLP);
        }

        const float FeedbackBusDucked = ShapedBusForTone * DuckGain;

        // --- IMMEDIATE REVERB INJECTION ---
        // Feed the instantaneous input to the FDN so the tail starts immediately,
        // independent of the pre-delay used for clean echoes.
        // Do not scale feedback amount by FeedbackBusDucked magnitude; FeedbackGain controls decay.
        FeedbackDelayNetwork::WriteFeedbackDistributed(FDNState,
                                                       1.0f,           // neutral scalar; not used in current implementation
                                                       DryInputMono);  // immediate input into FDN

        // --- Output mixing: crossfade clean pre-delay vs. FDN wet ---
        float WetForOutput = FeedbackBusDucked;

        if (!UsePreFiltering)
        {
            WetForOutput = Highpass::ProcessSample(Channels[0].PostHP, WetForOutput, AlphaHP);
            WetForOutput = Lowpass::ProcessSample(Channels[0].PostLP, WetForOutput, AlphaLP);
        }

        const float CleanDelayOut = PreDelayedSample;
        const float ReverbOut = WetForOutput * DuckGain;

        const float MorphWet = (AmountA * CleanDelayOut) + (AmountB * ReverbOut);

        // Stereo Haas / width on wet
        float WetL = MorphWet;
        float WetR = MorphWet;

        HaasStereoWidener::ProcessStereoSample(WetL,
                                               WetR,
                                               StereoWidth,
                                               Channels[0].Haas,
                                               Channels[NumChannels > 1 ? 1 : 0].Haas,
                                               WetL,
                                               WetR);

        // Mix
        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            float* WritePtr = AudioBuffer.getWritePointer(ChannelIndex);
            const float DrySample = WritePtr[SampleIndex];

            const float ChannelWet = (ChannelIndex == 0 ? WetL : WetR);
            const float MixedSample = (DryGain * DrySample) + (WetGain * ChannelWet);

            WritePtr[SampleIndex] = MixedSample;
        }

        DiffuserJitterPhase += DiffuserJitterPhaseIncrement;

        if (DiffuserJitterPhase > juce::MathConstants<float>::twoPi)
            DiffuserJitterPhase -= juce::MathConstants<float>::twoPi;
    }
}