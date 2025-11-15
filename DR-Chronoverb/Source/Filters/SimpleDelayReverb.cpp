#include "SimpleDelayReverb.h"
#include <algorithm>

// ============================== Construction / Setup ==============================

SimpleDelayReverb::SimpleDelayReverb()
{
}

SimpleDelayReverb::~SimpleDelayReverb()
{
}

void SimpleDelayReverb::PrepareToPlay(double NewSampleRate, float NewMaximumDelaySeconds)
{
    // Store sample rate and sizing constraints
    SampleRate = (NewSampleRate > 0.0 ? NewSampleRate : 44100.0);
    MaximumDelaySeconds = std::max(0.001f, NewMaximumDelaySeconds);

    // Derive a maximum spread window as a fraction of the maximum delay time (capped to 150 ms)
    // Larger nominal delays allow a larger spread before becoming unwieldy.
    MaximumSpreadSeconds = std::min(0.150f, 0.25f * MaximumDelaySeconds);

    // Compute the maximum delay buffer length in samples with safety margin:
    // nominal delay + full spread + look-ahead + extra safety
    const float SafetySeconds = 0.020f;
    float MaxTotalSeconds = MaximumDelaySeconds + MaximumSpreadSeconds + (0.5f * MaximumSpreadSeconds) + SafetySeconds;

    MaxDelayBufferSamples = std::max(1, static_cast<int>(std::ceil(MaxTotalSeconds * static_cast<float>(SampleRate))));

    // Allocate per-channel buffers on next ensureChannelState
    Channels.clear();

    // Initialize smoothed params to targets to avoid startup glides
    SmoothedDelayTimeSeconds = TargetDelayTimeSeconds.load();
    SmoothedDiffusionSize = TargetDiffusionSize.load();

    // Compute an initial tap layout from defaults
    recomputeTargetTapLayout();

    IsPrepared = true;
}

void SimpleDelayReverb::Reset()
{
    for (ChannelState& State : Channels)
    {
        std::fill(State.DelayBuffer.begin(), State.DelayBuffer.end(), 0.0f);
        State.WriteIndex = 0;
        State.FeedbackState = 0.0f;
    }
}

// ============================== Parameter Setters ==============================

void SimpleDelayReverb::SetDelayTime(float DelayTimeSeconds)
{
    // Clamp to the configured maximum
    float Clamped = juce::jlimit(0.0f, MaximumDelaySeconds, DelayTimeSeconds);
    TargetDelayTimeSeconds.store(Clamped, std::memory_order_relaxed);
}

void SimpleDelayReverb::SetDiffusionAmount(float DiffusionAmount)
{
    float Clamped = juce::jlimit(0.0f, 1.0f, DiffusionAmount);
    TargetDiffusionAmount.store(Clamped, std::memory_order_relaxed);
}

void SimpleDelayReverb::SetDiffusionSize(float DiffusionSize)
{
    float Clamped = juce::jlimit(0.0f, 1.0f, DiffusionSize);
    TargetDiffusionSize.store(Clamped, std::memory_order_relaxed);
}

void SimpleDelayReverb::SetDiffusionQuality(float DiffusionQuality)
{
    float Clamped = juce::jlimit(0.0f, 1.0f, DiffusionQuality);
    TargetDiffusionQuality.store(Clamped, std::memory_order_relaxed);

    // Update target tap layout immediately (density determined by quality)
    recomputeTargetTapLayout();
}

void SimpleDelayReverb::SetFeedbackTime(float FeedbackTimeSeconds)
{
    // Clamp a reasonable T60 range; 0 disables feedback
    float Clamped = juce::jlimit(0.0f, 10.0f, FeedbackTimeSeconds);
    TargetFeedbackTimeSeconds.store(Clamped, std::memory_order_relaxed);
}

// ============================== Internal Helpers ==============================

void SimpleDelayReverb::ensureChannelState(int RequiredChannels)
{
    if (!IsPrepared)
    {
        return;
    }

    if (static_cast<int>(Channels.size()) < RequiredChannels)
    {
        int OldSize = static_cast<int>(Channels.size());
        Channels.resize(RequiredChannels);

        for (int ChannelIndex = OldSize; ChannelIndex < RequiredChannels; ++ChannelIndex)
        {
            Channels[ChannelIndex].DelayBuffer.assign(static_cast<size_t>(MaxDelayBufferSamples), 0.0f);
            Channels[ChannelIndex].WriteIndex = 0;
            Channels[ChannelIndex].FeedbackState = 0.0f;
        }
    }
}

void SimpleDelayReverb::resizeDelayBuffers()
{
    if (!IsPrepared)
    {
        return;
    }

    for (ChannelState& State : Channels)
    {
        State.DelayBuffer.assign(static_cast<size_t>(MaxDelayBufferSamples), 0.0f);
        State.WriteIndex = 0;
        State.FeedbackState = 0.0f;
    }
}

int SimpleDelayReverb::qualityToTapPairs(float Quality) const
{
    // Map [0..1] to [1..8] pairs (i.e., 2..16 taps total, symmetric about center, excluding center)
    // Lower quality => fewer taps (sparser), higher quality => more taps (denser).
    int Pairs = 1 + static_cast<int>(std::round(Quality * 7.0f));
    return juce::jlimit(1, 8, Pairs);
}

void SimpleDelayReverb::recomputeTargetTapLayout()
{
    // Build symmetric, deterministic offsets in normalized units [-1..+1]
    // Based on a prime-like sequence to avoid harmonic reinforcement.
    const int PairCount = qualityToTapPairs(TargetDiffusionQuality.load(std::memory_order_relaxed));

    NormalizedSymmetricOffsets.clear();
    NormalizedSymmetricOffsets.reserve(static_cast<size_t>(PairCount * 2));

    // Determine normalization factor so the farthest tap does not exceed |1.0|
    const int MaxPrime = PrimeLikeSequence[std::min(PairCount - 1, 7)];

    for (int PairIndex = 0; PairIndex < PairCount; ++PairIndex)
    {
        const int PrimeValue = PrimeLikeSequence[PairIndex];
        float Normalized = static_cast<float>(PrimeValue) / static_cast<float>(MaxPrime);
        Normalized = juce::jlimit(0.0f, 1.0f, Normalized);

        // Negative and positive symmetric offsets (center excluded)
        NormalizedSymmetricOffsets.push_back(-Normalized);
        NormalizedSymmetricOffsets.push_back(+Normalized);
    }

    // Sort by absolute proximity to center so closest offsets contribute first
    std::sort(NormalizedSymmetricOffsets.begin(),
              NormalizedSymmetricOffsets.end(),
              [](float A, float B)
              {
                  return std::abs(A) < std::abs(B);
              });
}

float SimpleDelayReverb::computeDampingCoefficient(float CurrentSampleRate) const
{
    // Simple one-pole low-pass in the feedback path:
    // y[n] = y[n-1] + alpha * (x[n] - y[n-1]),  alpha in (0..1)
    //
    // We map alpha from a nominal cutoff in Hz using a crude approximation:
    // alpha = 1 - exp(-2*pi*Fc / Fs)
    //
    // For perceptual behavior:
    // - More diffusion amount => stronger damping (lower cutoff).
    // - Higher quality => slightly smoother tail (also lower cutoff).
    const float Amount = TargetDiffusionAmount.load(std::memory_order_relaxed);
    const float Quality = TargetDiffusionQuality.load(std::memory_order_relaxed);

    // Map amount and quality to a cutoff [3kHz .. 12kHz]
    float CutoffHz = juce::jmap(Amount, 0.0f, 1.0f, 12000.0f, 6000.0f);
    CutoffHz = juce::jmap(Quality, 0.0f, 1.0f, CutoffHz, CutoffHz * 0.8f);
    CutoffHz = juce::jlimit(1000.0f, 18000.0f, CutoffHz);

    float Alpha = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi * CutoffHz / static_cast<float>(CurrentSampleRate));
    Alpha = juce::jlimit(0.0f, 1.0f, Alpha);
    return Alpha;
}

float SimpleDelayReverb::t60ToFeedbackGain(float LoopSeconds, float T60Seconds) const
{
    // Convert desired 60 dB decay time to per-loop linear gain.
    // Guard: zero or tiny T60 => no feedback; tiny loop => clamp.
    if (T60Seconds <= 0.0f || LoopSeconds <= 0.0f)
    {
        return 0.0f;
    }

    float Gain = std::pow(10.0f, -3.0f * (LoopSeconds / T60Seconds));
    return juce::jlimit(0.0f, 0.9995f, Gain);
}

void SimpleDelayReverb::updateBlockSmoothing(int NumSamples)
{
    // Smooth delay time and spread over the block.
    // We compute per-sample smoothing inside the loop using these coefficients.

    // Clamp targets for safety
    float TargetDelay = juce::jlimit(0.0f, MaximumDelaySeconds, TargetDelayTimeSeconds.load(std::memory_order_relaxed));
    float TargetSize = juce::jlimit(0.0f, 1.0f, TargetDiffusionSize.load(std::memory_order_relaxed));

    // Initialize smoothed values closer to targets before loop to reduce lag on first sample
    SmoothedDelayTimeSeconds = smoothOnePole(SmoothedDelayTimeSeconds, TargetDelay, DelayTimeSmoothCoefficient);
    SmoothedDiffusionSize    = smoothOnePole(SmoothedDiffusionSize,    TargetSize, SizeSmoothCoefficient);

    juce::ignoreUnused(NumSamples);
}

inline float SimpleDelayReverb::smoothOnePole(float Current, float Target, float Coefficient) const
{
    // One-pole lag towards target: y += a * (t - y)
    return Current + Coefficient * (Target - Current);
}

inline float SimpleDelayReverb::readFromDelayBuffer(const ChannelState& State, float DelayInSamples) const
{
    // Enforce valid positive delay
    if (DelayInSamples < 0.0f)
    {
        DelayInSamples = 0.0f;
    }

    const int BufferSize = static_cast<int>(State.DelayBuffer.size());
    if (BufferSize <= 1)
    {
        return 0.0f;
    }

    // Read position is WriteIndex - DelayInSamples
    float ReadPosition = static_cast<float>(State.WriteIndex) - DelayInSamples;

    // Wrap into [0..BufferSize)
    while (ReadPosition < 0.0f)
    {
        ReadPosition += static_cast<float>(BufferSize);
    }

    // Linear interpolation
    int IndexA = static_cast<int>(ReadPosition) % BufferSize;
    int IndexB = (IndexA + 1) % BufferSize;
    float Frac = ReadPosition - static_cast<float>(IndexA);

    const float* Data = State.DelayBuffer.data();
    float SampleA = Data[IndexA];
    float SampleB = Data[IndexB];

    return SampleA + (SampleB - SampleA) * Frac;
}

inline void SimpleDelayReverb::writeToDelayBuffer(ChannelState& State, float Sample)
{
    const int BufferSize = static_cast<int>(State.DelayBuffer.size());
    if (BufferSize <= 0)
    {
        return;
    }

    State.DelayBuffer[State.WriteIndex] = Sample;

    State.WriteIndex++;
    if (State.WriteIndex >= BufferSize)
    {
        State.WriteIndex = 0;
    }
}

// ============================== Processing ==============================

void SimpleDelayReverb::ProcessBlock(juce::AudioBuffer<float>& AudioBuffer)
{
    if (!IsPrepared)
    {
        return;
    }

    const int NumChannels = AudioBuffer.getNumChannels();
    const int NumSamples = AudioBuffer.getNumSamples();

    ensureChannelState(NumChannels);

    // Cache parameters for this block
    const float Amount = TargetDiffusionAmount.load(std::memory_order_relaxed);
    const float Quality = TargetDiffusionQuality.load(std::memory_order_relaxed);
    const float T60Seconds = TargetFeedbackTimeSeconds.load(std::memory_order_relaxed);

    // Equal-power crossfade coefficients for smoother morph (Amount in [0..1])
    const float AmountA = std::cos(Amount * juce::MathConstants<float>::halfPi); // base tap weight
    const float AmountB = std::sin(Amount * juce::MathConstants<float>::halfPi); // diffused cluster weight

    // Feedback damping coefficient (one-pole LPF)
    const float DampingAlpha = computeDampingCoefficient(static_cast<float>(SampleRate));

    // Per-block smoothing priming
    updateBlockSmoothing(NumSamples);

    // Precompute constants for offset generation
    // Spread (in seconds) scales with user size and the configured maximum spread window.
    // Look-ahead shift is half the maximum spread, in samples, to allow negative offsets causally.
    const float MaxSpreadSamples = secondsToSamples(MaximumSpreadSeconds);
    const float LookaheadSamples = 0.5f * MaxSpreadSamples;

    // Sum of absolute weights for normalization (taps nearer center get slightly more weight)
    // We compute static weights from the order (closer to center -> higher weight).
    const int TotalTaps = static_cast<int>(NormalizedSymmetricOffsets.size());
    std::vector<float> TapWeights(static_cast<size_t>(TotalTaps), 1.0f);
    {
        // Weighting: inverse with rank by absolute offset (already sorted by abs ascending)
        // Start at 1.0, gently fall towards edges
        float Weight = 1.0f;
        const float FalloffPerTap = 0.08f; // gentle preference for inner taps
        for (int TapIndex = 0; TapIndex < TotalTaps; ++TapIndex)
        {
            TapWeights[static_cast<size_t>(TapIndex)] = Weight;
            Weight = std::max(0.25f, Weight - FalloffPerTap);
        }
    }
    const float WeightSum = std::accumulate(TapWeights.begin(), TapWeights.end(), 0.0f);
    const float WeightNorm = (WeightSum > 0.0f ? 1.0f / WeightSum : 1.0f);

    // Main processing loop
    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // Smooth delay time and spread towards targets each sample to reduce zipper/pitch artifacts
        const float TargetDelayTimeSecondsLocal = TargetDelayTimeSeconds.load(std::memory_order_relaxed);
        const float TargetSizeLocal = TargetDiffusionSize.load(std::memory_order_relaxed);

        SmoothedDelayTimeSeconds = smoothOnePole(SmoothedDelayTimeSeconds, TargetDelayTimeSecondsLocal, DelayTimeSmoothCoefficient);
        SmoothedDiffusionSize    = smoothOnePole(SmoothedDiffusionSize,    TargetSizeLocal,             SizeSmoothCoefficient);

        // Base delay and dynamic spread
        const float BaseDelaySamples = secondsToSamples(juce::jlimit(0.0f, MaximumDelaySeconds, SmoothedDelayTimeSeconds));
        const float SpreadSamples    = secondsToSamples(juce::jlimit(0.0f, MaximumSpreadSeconds, SmoothedDiffusionSize * MaximumSpreadSeconds));

        // Compute per-loop feedback gain from T60 (use current nominal delay as loop period)
        const float LoopSeconds = std::max(1.0e-4f, SmoothedDelayTimeSeconds);
        const float FeedbackGain = t60ToFeedbackGain(LoopSeconds, T60Seconds);

        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            float* ChannelData = AudioBuffer.getWritePointer(ChannelIndex);
            ChannelState& State = Channels[ChannelIndex];

            // 1) Input Signal Acquisition and Initial Delay
            //    - Acquire the dry input sample.
            const float InputSample = ChannelData[SampleIndex];

            // 2) Diffusion Parameter Scaling
            //    - Already mapped above via AmountA/AmountB (equal-power morph), SpreadSamples, and tap count via Quality.

            // --- Base nominal delay tap (no diffusion) ---
            const float BaseTap = readFromDelayBuffer(State, BaseDelaySamples);

            // 3) Symmetric Cluster Generation (diffused cluster around nominal, using look-ahead for negative offsets)
            float ClusterSum = 0.0f;

            for (int TapIndex = 0; TapIndex < TotalTaps; ++TapIndex)
            {
                const float NormalizedOffset = NormalizedSymmetricOffsets[static_cast<size_t>(TapIndex)];
                const float SignedOffsetSamples = NormalizedOffset * SpreadSamples;

                // Effective read delay time in samples:
                // nominal + lookahead (to allow negative offsets) + signed offset
                float EffectiveDelaySamples = BaseDelaySamples + LookaheadSamples + SignedOffsetSamples;

                // Read from delay line at fractional position (linear interpolation)
                float TapSample = readFromDelayBuffer(State, EffectiveDelaySamples);

                // Apply tap weight
                TapSample *= TapWeights[static_cast<size_t>(TapIndex)];

                ClusterSum += TapSample;
            }

            // Normalize summed taps to avoid level build-up as density increases
            const float DiffusedCluster = ClusterSum * WeightNorm;

            // Crossfade between base tap (pure delay) and cluster (full diffusion)
            const float WetEcho = (AmountA * BaseTap) + (AmountB * DiffusedCluster);

            // 4) Density Buildup via Feedback
            //    - Low-pass damping in the feedback loop for a natural tail.
            //    - Feed a morphing mix back into the line:
            //      base repeats at Amount=0, diffused tail at Amount=1.
            const float FeedbackInput = WetEcho;

            // One-pole low-pass on feedback content
            State.FeedbackState = State.FeedbackState + DampingAlpha * (FeedbackInput - State.FeedbackState);

            const float FeedbackSample = State.FeedbackState * FeedbackGain;

            // Compose the signal to write into the delay line:
            // dry input plus feedback recirculation.
            const float DelayLineInput = InputSample + FeedbackSample;

            // Write and advance the circular buffer
            writeToDelayBuffer(State, DelayLineInput);

            // 5) Pitch Modulation Handling
            //    - The smoothed changes in BaseDelaySamples and SpreadSamples implicitly cause
            //      gentle pitch shifts when parameters move. Our one-pole smoothing mitigates artifacts.

            // Wet signal output is the current crossfaded echo
            const float WetSample = WetEcho;

            // In-place: add wet on top of dry (leave external dry/wet mixing to the caller/host)
            ChannelData[SampleIndex] += WetSample;
        }
    }
}