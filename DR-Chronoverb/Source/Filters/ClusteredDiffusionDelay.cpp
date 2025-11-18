#include <algorithm>
#include "ClusteredDiffusionDelay.h"

// ================================= Construction / Destruction =================================

ClusteredDiffusionDelay::ClusteredDiffusionDelay()
{
    // Constructor: leave atomic defaults; actual allocation happens in PrepareToPlay.
}

ClusteredDiffusionDelay::~ClusteredDiffusionDelay()
{
    // Destructor: vectors and RAII members clean themselves.
}

// ================================= Preparation / Reset =================================

void ClusteredDiffusionDelay::PrepareToPlay(double NewSampleRate, float NewMaximumDelaySeconds)
{
    // Store validated sample rate (fallback to 44100.0 if invalid)
    SampleRate = (NewSampleRate > 0.0 ? NewSampleRate : 44100.0);

    // Store maximum nominal delay headroom (clamp to small positive value)
    MaximumDelaySeconds = std::max(0.001f, NewMaximumDelaySeconds);

    // Derive maximum spread window (capped at 150 ms, scaled by nominal delay)
    MaximumSpreadSeconds = std::min(0.150f, 0.25f * MaximumDelaySeconds);

    // Compute total worst-case seconds for buffer: nominal + spread + look-ahead shift + safety margin
    const float SafetySeconds = 0.020f;
    float MaxTotalSeconds = MaximumDelaySeconds
                          + MaximumSpreadSeconds
                          + (0.5f * MaximumSpreadSeconds)
                          + SafetySeconds;

    // Convert worst-case time to sample count with ceiling and enforce minimum of 1
    MaxDelayBufferSamples = std::max(1, static_cast<int>(std::ceil(MaxTotalSeconds * static_cast<float>(SampleRate))));

    // Clear existing channel states (will be recreated lazily)
    Channels.clear();

    // Initialize smoothed parameters directly to targets (avoid startup interpolation glide)
    SmoothedDelayTimeSeconds = TargetDelayTimeSeconds.load();
    SmoothedDiffusionSize    = TargetDiffusionSize.load();

    // Build initial symmetric tap layout from current diffusion quality
    recomputeTargetTapLayout();

    // Prepare stereo widener (Haas max fixed 40 ms like previous implementation)
    StereoWidthProcessor.prepareToPlay(SampleRate, 40.0f);

    // Mark prepared
    IsPrepared = true;
}

void ClusteredDiffusionDelay::Reset()
{
    // Reset delay lines and filter states
    for (std::unique_ptr<ChannelState>& ChannelPtr : Channels)
    {
        ChannelState& Channel = *ChannelPtr; // Dereference unique_ptr to access channel state

        std::fill(Channel.DelayBuffer.begin(), Channel.DelayBuffer.end(), 0.0f);
        Channel.WriteIndex = 0;

        if (Channel.FiltersPrepared)
        {
            Channel.FeedbackDampingLowpass.reset();
            Channel.PreHighpassFilter.reset();
            Channel.PreLowpassFilter.reset();
        }
    }

    // Clear scratch wet buffer (not strictly required)
    WetScratchBuffer.clear();
}

// ================================= Parameter Setters =================================

void ClusteredDiffusionDelay::SetDelayTime(float DelayTimeSeconds)
{
    // Clamp delay time to [0 .. MaximumDelaySeconds]
    float Clamped = juce::jlimit(0.0f, MaximumDelaySeconds, DelayTimeSeconds);
    TargetDelayTimeSeconds.store(Clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetFeedbackTime(float FeedbackTimeSeconds)
{
    // Clamp T60 range to [0 .. 10] seconds
    float Clamped = juce::jlimit(0.0f, 10.0f, FeedbackTimeSeconds);
    TargetFeedbackTimeSeconds.store(Clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDiffusionAmount(float DiffusionAmount)
{
    // Clamp diffusion amount [0 .. 1]
    float Clamped = juce::jlimit(0.0f, 1.0f, DiffusionAmount);
    TargetDiffusionAmount.store(Clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDiffusionSize(float DiffusionSize)
{
    // Clamp diffusion size [0 .. 1]
    float Clamped = juce::jlimit(0.0f, 1.0f, DiffusionSize);
    TargetDiffusionSize.store(Clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetDiffusionQuality(float DiffusionQuality)
{
    // Clamp diffusion quality [0 .. 1]
    float Clamped = juce::jlimit(0.0f, 1.0f, DiffusionQuality);
    TargetDiffusionQuality.store(Clamped, std::memory_order_relaxed);

    // Recompute symmetric tap offsets (density changes with quality)
    recomputeTargetTapLayout();
}

void ClusteredDiffusionDelay::SetDryWetMix(float DryWetMix)
{
    // Clamp dry/wet mix [0 .. 1]
    float Clamped = juce::jlimit(0.0f, 1.0f, DryWetMix);
    TargetDryWetMix.store(Clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetStereoSpread(float StereoWidth)
{
    // Clamp stereo width [-1 .. 1]
    float Clamped = juce::jlimit(-1.0f, 1.0f, StereoWidth);
    TargetStereoWidth.store(Clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetLowpassDecay(float DecayAmount)
{
    // Clamp lowpass decay amount [0 .. 1]
    float Clamped = juce::jlimit(0.0f, 1.0f, DecayAmount);
    TargetPreLowpassDecayAmount.store(Clamped, std::memory_order_relaxed);
}

void ClusteredDiffusionDelay::SetHighpassDecay(float DecayAmount)
{
    // Clamp highpass decay amount [0 .. 1]
    float Clamped = juce::jlimit(0.0f, 1.0f, DecayAmount);
    TargetPreHighpassDecayAmount.store(Clamped, std::memory_order_relaxed);
}

// ================================= Internal Helpers =================================

void ClusteredDiffusionDelay::ensureChannelState(int RequiredChannels)
{
    // Do nothing if not prepared
    if (!IsPrepared)
    {
        return;
    }

    // Grow channel list if needed, using unique_ptr so we never copy/move ChannelState itself.
    while (static_cast<int>(Channels.size()) < RequiredChannels)
    {
        // Create a new ChannelState instance on the heap
        std::unique_ptr<ChannelState> NewChannel = std::make_unique<ChannelState>();

        // Allocate delay buffer with maximum size for the new channel
        NewChannel->DelayBuffer.assign(static_cast<size_t>(MaxDelayBufferSamples), 0.0f);
        NewChannel->WriteIndex = 0;

        // Prepare modular filters for this channel
        NewChannel->FeedbackDampingLowpass.prepareToPlay(SampleRate);
        NewChannel->PreHighpassFilter.prepareToPlay(SampleRate);
        NewChannel->PreLowpassFilter.prepareToPlay(SampleRate);

        // (Optional future diffusion allpasses can be prepared here)

        NewChannel->FiltersPrepared = true;

        // Store the prepared channel into the vector (moves the pointer, not the state)
        Channels.emplace_back(std::move(NewChannel));
    }

    // Scratch wet buffer size is managed per ProcessBlock; nothing required here.
}

int ClusteredDiffusionDelay::qualityToTapPairs(float Quality) const
{
    // Map [0..1] → integer pairs [1..8]
    int Pairs = 1 + static_cast<int>(std::round(Quality * 7.0f));
    return juce::jlimit(1, 8, Pairs);
}

void ClusteredDiffusionDelay::recomputeTargetTapLayout()
{
    // Determine number of symmetric tap pairs from quality
    const int PairCount = qualityToTapPairs(TargetDiffusionQuality.load(std::memory_order_relaxed));

    // Clear current layout vector
    NormalizedSymmetricOffsets.clear();
    NormalizedSymmetricOffsets.reserve(static_cast<size_t>(PairCount * 2));

    // Maximum prime value for normalization
    const int MaxPrime = PrimeLikeSequence[std::min(PairCount - 1, 7)];

    // Generate negative/positive normalized offsets (exclude center = 0)
    for (int PairIndex = 0; PairIndex < PairCount; ++PairIndex)
    {
        const int PrimeValue = PrimeLikeSequence[PairIndex];
        float Normalized = static_cast<float>(PrimeValue) / static_cast<float>(MaxPrime);
        Normalized = juce::jlimit(0.0f, 1.0f, Normalized);

        NormalizedSymmetricOffsets.push_back(-Normalized);
        NormalizedSymmetricOffsets.push_back(+Normalized);
    }

    // Sort offsets by |distance| ascending so near-center taps appear first
    std::sort(NormalizedSymmetricOffsets.begin(),
              NormalizedSymmetricOffsets.end(),
              [](float A, float B)
              {
                  return std::abs(A) < std::abs(B);
              });
}

inline float ClusteredDiffusionDelay::readFromDelayBuffer(const ChannelState& State, float DelayInSamples)
{
    // Ensure non-negative delay
    if (DelayInSamples < 0.0f)
    {
        DelayInSamples = 0.0f;
    }

    const int BufferSize = static_cast<int>(State.DelayBuffer.size());

    if (BufferSize <= 1)
    {
        return 0.0f;
    }

    // Compute fractional read position relative to write pointer
    float ReadPosition = static_cast<float>(State.WriteIndex) - DelayInSamples;

    // Wrap read position into valid range [0 .. BufferSize)
    while (ReadPosition < 0.0f)
    {
        ReadPosition += static_cast<float>(BufferSize);
    }

    // Integer indices and fractional part for linear interpolation
    int IndexA = static_cast<int>(ReadPosition) % BufferSize;
    int IndexB = (IndexA + 1) % BufferSize;
    float Fraction = ReadPosition - static_cast<float>(IndexA);

    // Fetch samples
    const float* Data = State.DelayBuffer.data();
    float SampleA = Data[IndexA];
    float SampleB = Data[IndexB];

    // Interpolate and return
    return SampleA + (SampleB - SampleA) * Fraction;
}

inline void ClusteredDiffusionDelay::writeToDelayBuffer(ChannelState& State, float SampleValue)
{
    // Guard buffer size
    const int BufferSize = static_cast<int>(State.DelayBuffer.size());

    if (BufferSize <= 0)
    {
        return;
    }

    // Write current sample at write index
    State.DelayBuffer[State.WriteIndex] = SampleValue;

    // Advance and wrap write index
    State.WriteIndex++;

    if (State.WriteIndex >= BufferSize)
    {
        State.WriteIndex = 0;
    }
}

inline float ClusteredDiffusionDelay::smoothOnePole(float CurrentValue, float TargetValue, float Coefficient)
{
    // One-pole smoothing: y += a * (t - y)
    return CurrentValue + Coefficient * (TargetValue - CurrentValue);
}

float ClusteredDiffusionDelay::computeDampingCutoffHz() const
{
    // Recreate previous mapping logic for feedback damping cutoff
    const float DiffusionAmount = TargetDiffusionAmount.load(std::memory_order_relaxed);
    const float DiffusionQuality = TargetDiffusionQuality.load(std::memory_order_relaxed);

    // Start with amount mapping: [0 → 12kHz, 1 → 6kHz]
    float CutoffHz = juce::jmap(DiffusionAmount, 0.0f, 1.0f, 12000.0f, 6000.0f);

    // Quality reduces cutoff slightly for smoother tail
    CutoffHz = juce::jmap(DiffusionQuality, 0.0f, 1.0f, CutoffHz, CutoffHz * 0.8f);

    // Clamp final cutoff
    CutoffHz = juce::jlimit(1000.0f, 18000.0f, CutoffHz);

    return CutoffHz;
}

void ClusteredDiffusionDelay::computeDiffusionCrossfade(float DiffusionAmount,
                                                        float& OutAmountA,
                                                        float& OutAmountB) const
{
    // Equal-power crossfade (base tap vs diffused cluster)
    OutAmountA = std::cos(DiffusionAmount * juce::MathConstants<float>::halfPi);
    OutAmountB = std::sin(DiffusionAmount * juce::MathConstants<float>::halfPi);
}

float ClusteredDiffusionDelay::t60ToFeedbackGain(float LoopSeconds, float T60Seconds)
{
    // Zero or invalid values => no feedback
    if (T60Seconds <= 0.0f || LoopSeconds <= 0.0f)
    {
        return 0.0f;
    }

    // Standard 60 dB decay mapping
    float Gain = std::pow(10.0f, -3.0f * (LoopSeconds / T60Seconds));

    // Clamp just below 1.0 for stability
    return juce::jlimit(0.0f, 0.9995f, Gain);
}

void ClusteredDiffusionDelay::mapDecayAmountsToCutoffs(float LowpassDecayAmount,
                                                       float HighpassDecayAmount,
                                                       float& OutLowpassCutoffHz,
                                                       float& OutHighpassCutoffHz) const
{
    // Map lowpass decay amount: 0 → 18kHz (bright), 1 → 1kHz (darker)
    float LPCutoffHz = juce::jmap(LowpassDecayAmount, 0.0f, 1.0f, 18000.0f, 1000.0f);
    LPCutoffHz = juce::jlimit(100.0f, 20000.0f, LPCutoffHz);

    // Map highpass decay amount: 0 → 20 Hz (full body), 1 → 2kHz (thin)
    float HPCutoffHz = juce::jmap(HighpassDecayAmount, 0.0f, 1.0f, 20.0f, 2000.0f);
    HPCutoffHz = juce::jlimit(10.0f, 8000.0f, HPCutoffHz);

    OutLowpassCutoffHz = LPCutoffHz;
    OutHighpassCutoffHz = HPCutoffHz;
}

void ClusteredDiffusionDelay::updatePreFilterCutoffs()
{
    // Read decay amounts atomically
    float LPAmount = TargetPreLowpassDecayAmount.load(std::memory_order_relaxed);
    float HPAmount = TargetPreHighpassDecayAmount.load(std::memory_order_relaxed);

    // Map amounts to actual cutoff frequencies
    float LPCutoffHz = 0.0f;
    float HPCutoffHz = 0.0f;
    mapDecayAmountsToCutoffs(LPAmount, HPAmount, LPCutoffHz, HPCutoffHz);

    // Apply cutoffs to every channel's pre filters
    for (std::unique_ptr<ChannelState>& ChannelPtr : Channels)
    {
        ChannelState& Channel = *ChannelPtr;

        if (Channel.FiltersPrepared)
        {
            Channel.PreHighpassFilter.setCutoffFrequency(HPCutoffHz);
            Channel.PreLowpassFilter.setCutoffFrequency(LPCutoffHz);
        }
    }
}

void ClusteredDiffusionDelay::updateFeedbackDampingCutoff()
{
    // Compute damping cutoff based on amount/quality
    float DampingCutoffHz = computeDampingCutoffHz();

    // Update each channel's feedback damping lowpass
    for (std::unique_ptr<ChannelState>& ChannelPtr : Channels)
    {
        ChannelState& Channel = *ChannelPtr;

        if (Channel.FiltersPrepared)
        {
            Channel.FeedbackDampingLowpass.setCutoffFrequency(DampingCutoffHz);
        }
    }
}

void ClusteredDiffusionDelay::updateBlockSmoothing(int NumSamples)
{
    // Pull atomic targets
    float TargetDelay = juce::jlimit(0.0f, MaximumDelaySeconds, TargetDelayTimeSeconds.load(std::memory_order_relaxed));
    float TargetSize  = juce::jlimit(0.0f, 1.0f, TargetDiffusionSize.load(std::memory_order_relaxed));

    // Prime smoothed values slightly towards targets before sample loop
    SmoothedDelayTimeSeconds = smoothOnePole(SmoothedDelayTimeSeconds, TargetDelay, DelayTimeSmoothCoefficient);
    SmoothedDiffusionSize    = smoothOnePole(SmoothedDiffusionSize,    TargetSize, SizeSmoothCoefficient);

    juce::ignoreUnused(NumSamples);
}

// ================================= Processing =================================

void ClusteredDiffusionDelay::ProcessBlock(juce::AudioBuffer<float>& AudioBuffer)
{
    // Abort if not prepared
    if (!IsPrepared)
    {
        return;
    }

    const int NumChannels = AudioBuffer.getNumChannels();
    const int NumSamples  = AudioBuffer.getNumSamples();

    // Ensure channel states exist for current channel count
    ensureChannelState(NumChannels);

    // Resize scratch wet buffer to match current block size
    WetScratchBuffer.setSize(NumChannels, NumSamples, false, false, true);

    // Cache frequently used atomic parameters
    const float DiffusionAmount      = TargetDiffusionAmount.load(std::memory_order_relaxed);
    const float DiffusionQuality     = TargetDiffusionQuality.load(std::memory_order_relaxed);
    const float FeedbackT60Seconds   = TargetFeedbackTimeSeconds.load(std::memory_order_relaxed);
    const float DryWetMix            = TargetDryWetMix.load(std::memory_order_relaxed);
    const float StereoWidthValue     = TargetStereoWidth.load(std::memory_order_relaxed);

    // Compute equal-power dry/wet gains
    const float DryGain = std::cos(DryWetMix * juce::MathConstants<float>::halfPi);
    const float WetGain = std::sin(DryWetMix * juce::MathConstants<float>::halfPi);

    // Update per-block smoothing priming
    updateBlockSmoothing(NumSamples);

    // Update pre-filter cutoffs from decay amounts
    updatePreFilterCutoffs();

    // Update feedback damping filter cutoff from amount/quality
    updateFeedbackDampingCutoff();

    // Compute diffusion crossfade coefficients
    float AmountA = 0.0f;
    float AmountB = 0.0f;
    computeDiffusionCrossfade(DiffusionAmount, AmountA, AmountB);

    // Precompute cluster tap weights (same logic as previous implementation)
    const int TotalTaps = static_cast<int>(NormalizedSymmetricOffsets.size());
    std::vector<float> TapWeights(static_cast<size_t>(TotalTaps), 1.0f);

    {
        float CurrentWeight = 1.0f;
        const float FalloffPerTap = 0.08f;

        for (int TapIndex = 0; TapIndex < TotalTaps; ++TapIndex)
        {
            TapWeights[static_cast<size_t>(TapIndex)] = CurrentWeight;
            CurrentWeight = std::max(0.25f, CurrentWeight - FalloffPerTap);
        }
    }

    // Normalize weights
    const float WeightSum = std::accumulate(TapWeights.begin(), TapWeights.end(), 0.0f);
    const float WeightNormalization = (WeightSum > 0.0f ? 1.0f / WeightSum : 1.0f);

    // Spread constants
    const float MaxSpreadSamples = secondsToSamples(MaximumSpreadSeconds);
    const float LookaheadSamples = 0.5f * MaxSpreadSamples;

    // Cache per-sample feedback gains to keep parity with the old implementation
    std::vector<float> FeedbackGainPerSample(static_cast<size_t>(NumSamples), 0.0f);

    // Main per-sample processing loop (Stage A: build wet; also compute per-sample feedback gain)
    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // Smooth delay and diffusion size each sample
        const float LocalTargetDelaySeconds = TargetDelayTimeSeconds.load(std::memory_order_relaxed);
        const float LocalTargetSize         = TargetDiffusionSize.load(std::memory_order_relaxed);

        SmoothedDelayTimeSeconds = smoothOnePole(SmoothedDelayTimeSeconds, LocalTargetDelaySeconds, DelayTimeSmoothCoefficient);
        SmoothedDiffusionSize    = smoothOnePole(SmoothedDiffusionSize,    LocalTargetSize,         SizeSmoothCoefficient);

        // Convert smoothed times to sample offsets
        const float BaseDelaySamples = secondsToSamples(juce::jlimit(0.0f, MaximumDelaySeconds, SmoothedDelayTimeSeconds));
        const float SpreadSamples    = secondsToSamples(juce::jlimit(0.0f, MaximumSpreadSeconds, SmoothedDiffusionSize * MaximumSpreadSeconds));

        // Loop period in seconds for feedback gain calculation (sample-accurate)
        const float LoopSeconds = std::max(1.0e-4f, SmoothedDelayTimeSeconds);

        // Store feedback gain for this sample (sample-accurate parity with old code)
        FeedbackGainPerSample[static_cast<size_t>(SampleIndex)] = t60ToFeedbackGain(LoopSeconds, FeedbackT60Seconds);

        // --- Build wet (pre-stereo) echo for each channel ---
        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            // Access channel state and input sample
            ChannelState& Channel = *Channels[ChannelIndex]; // Dereference unique_ptr
            float* ChannelWritePtr = AudioBuffer.getWritePointer(ChannelIndex);
            const float InputSample = ChannelWritePtr[SampleIndex];
            juce::ignoreUnused(InputSample); // InputSample not needed to read taps

            // Base nominal tap read
            const float BaseTapSample = readFromDelayBuffer(Channel, BaseDelaySamples);

            // Accumulate symmetric cluster taps
            float ClusterAccumulation = 0.0f;

            for (int TapIndex = 0; TapIndex < TotalTaps; ++TapIndex)
            {
                const float NormalizedOffset = NormalizedSymmetricOffsets[static_cast<size_t>(TapIndex)];
                const float SignedOffsetSamples = NormalizedOffset * SpreadSamples;

                // Effective delay includes lookahead shift for causal negative offsets
                float EffectiveDelaySamples = BaseDelaySamples + LookaheadSamples + SignedOffsetSamples;

                float TapSample = readFromDelayBuffer(Channel, EffectiveDelaySamples);

                // Apply weight
                TapSample *= TapWeights[static_cast<size_t>(TapIndex)];

                ClusterAccumulation += TapSample;
            }

            // Normalize cluster sum
            const float DiffusedClusterSample = ClusterAccumulation * WeightNormalization;

            // Crossfade between base tap (AmountA) and cluster (AmountB)
            const float WetEchoSample = (AmountA * BaseTapSample) + (AmountB * DiffusedClusterSample);

            // Store wet echo into scratch wet buffer before stereo width processing
            WetScratchBuffer.getWritePointer(ChannelIndex)[SampleIndex] = WetEchoSample;
        }
    }

    // --- Apply stereo width once for full wet buffer block ---
    if (StereoWidthValue != 0.0f)
    {
        StereoWidthProcessor.setStereoWidth(StereoWidthValue);
        StereoWidthProcessor.processBlock(WetScratchBuffer);
    }

    // --- Stage C: Feedback, filtering, delay write, output mix (now width-adjusted wet available) ---
    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // Fetch per-sample feedback gain that matches the smoothed delay used when taps were read
        const float FeedbackGainForThisSample = FeedbackGainPerSample[static_cast<size_t>(SampleIndex)];

        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            ChannelState& Channel = *Channels[ChannelIndex]; // Dereference unique_ptr

            float* ChannelWritePtr = AudioBuffer.getWritePointer(ChannelIndex);
            const float InputSample = ChannelWritePtr[SampleIndex];

            const float WetStereoSample = WetScratchBuffer.getReadPointer(ChannelIndex)[SampleIndex];

            // Feedback damping lowpass (modular replacement for previous one-pole)
            float DampedFeedbackSample = Channel.FeedbackDampingLowpass.processSample(WetStereoSample);

            // Apply feedback gain (sample-accurate T60 mapping)
            float FeedbackSample = DampedFeedbackSample * FeedbackGainForThisSample;

            // Pre highpass first (same order as before)
            float AfterHighpass = Channel.PreHighpassFilter.processSample(FeedbackSample);

            // Then pre lowpass shaping
            float AfterLowpass = Channel.PreLowpassFilter.processSample(AfterHighpass);

            // Compose delay line input (dry + shaped feedback)
            const float DelayLineInputSample = InputSample + AfterLowpass;

            // Write into delay buffer
            writeToDelayBuffer(Channel, DelayLineInputSample);

            // Dry/Wet mix (equal-power law)
            const float MixedOutputSample = (DryGain * InputSample) + (WetGain * WetStereoSample);

            // Store final output
            ChannelWritePtr[SampleIndex] = MixedOutputSample;
        }
    }
}