#include "DiffusedDelayReverb.h"

//==============================================================================
DiffusedDelayReverb::DiffusedDelayReverb()
    : feedbackMatrix(numFdnChannels, numFdnChannels)
{
    UpdateFeedbackMatrix();
    UpdateStereoMixMatrices();

    // Prime delays for FDN (in milliseconds, will be scaled)
    const std::array<int, numFdnChannels> primeDelaysMs = { 29, 37, 41, 53 };

    for (int Index = 0; Index < numFdnChannels; ++Index)
    {
        delaySamples[Index] = static_cast<int>(primeDelaysMs[Index] * 0.001f * sampleRate);
    }

    smoothedDiffusionAmount.reset(44100, 0.05f);
    smoothedWetDry.reset(44100, 0.05f);
}

//==============================================================================
void DiffusedDelayReverb::PrepareToPlay(double newSampleRate, float maxDelaySeconds)
{
    sampleRate = static_cast<float>(newSampleRate);
    maxDelayTimeSeconds = maxDelaySeconds;

    // Resize FDN delay buffer (power of two)
    const int MaxFdnSamples = static_cast<int>(maxDelayTimeSeconds * sampleRate) + 1;
    const int FdnBufferSize = juce::nextPowerOfTwo(MaxFdnSamples);

    delayBuffer.setSize(numFdnChannels, FdnBufferSize, false, true, false);
    delayBuffer.clear();
    std::fill(writePos.begin(), writePos.end(), 0);

    // Resize dedicated echo buffer (stereo)
    const int MaxEchoSamples = juce::nextPowerOfTwo(static_cast<int>(maxDelayTimeSeconds * sampleRate) + 1);
    echoBuffer.setSize(2, MaxEchoSamples, false, true, false);
    echoBuffer.clear();
    echoWritePos = { 0, 0 };

    // --- Pre-Echo Smear Diffusers Setup ---
    const int MaxSmearDelaySamples = static_cast<int>(0.020f * sampleRate); // 20 ms headroom

    preEchoSmearLeftA.Prepare(sampleRate, MaxSmearDelaySamples);
    preEchoSmearLeftB.Prepare(sampleRate, MaxSmearDelaySamples);
    preEchoSmearRightA.Prepare(sampleRate, MaxSmearDelaySamples);
    preEchoSmearRightB.Prepare(sampleRate, MaxSmearDelaySamples);

    UpdateSmearAndAdvance();

    // Keep inputBuffer (not used for echo now), clear it
    const int MaxInputSamples = juce::nextPowerOfTwo(static_cast<int>(maxPreDelayMs * 0.001f * sampleRate) + FdnBufferSize);
    inputBuffer.setSize(1, MaxInputSamples, false, true, false);
    inputBuffer.clear();
    inputWritePos = 0;

    // Update FDN delays
    UpdateDelayBuffer();

    // Prepare diffusion stages (one per channel)
    diffusionStageLeft.Prepare(sampleRate, diffusionSize, diffusionQuality);
    diffusionStageRight.Prepare(sampleRate, diffusionSize, diffusionQuality);

    // Reset smoothing
    smoothedDiffusionAmount.setCurrentAndTargetValue(diffusionAmount);
    smoothedWetDry.setCurrentAndTargetValue(wetDryMix);
}

//==============================================================================
void DiffusedDelayReverb::SetDelayTime(float timeSeconds)
{
    delayTimeSeconds = juce::jlimit(0.001f, maxDelayTimeSeconds, timeSeconds);
    UpdateDelayBuffer();
    UpdateSmearAndAdvance();
}

void DiffusedDelayReverb::SetFeedbackTime(float feedback)
{
    feedbackTimeSeconds = feedback;
    UpdateDelayBuffer();
}

void DiffusedDelayReverb::SetDiffusionAmount(float amount)
{
    diffusionAmount = juce::jlimit(0.0f, 1.0f, amount);
    smoothedDiffusionAmount.setTargetValue(diffusionAmount);
    UpdateSmearAndAdvance();
}

void DiffusedDelayReverb::SetDiffusionSize(float size)
{
    diffusionSize = juce::jlimit(0.0f, 1.0f, size);
    UpdateDiffusionNetwork();
    UpdateSmearAndAdvance();
}

void DiffusedDelayReverb::SetDiffusionQuality(float quality)
{
    diffusionQuality = juce::jlimit(0.0f, 1.0f, quality);
    diffusionStageLeft.Prepare(sampleRate, diffusionSize, diffusionQuality);
    diffusionStageRight.Prepare(sampleRate, diffusionSize, diffusionQuality);
    UpdateSmearAndAdvance();
}

void DiffusedDelayReverb::SetDryWetMix(float mix)
{
    wetDryMix = juce::jlimit(0.0f, 1.0f, mix);
    smoothedWetDry.setTargetValue(wetDryMix);
}

//==============================================================================
void DiffusedDelayReverb::ProcessBlock(juce::AudioBuffer<float>& AudioBuffer)
{
    const int NumSamples  = AudioBuffer.getNumSamples();
    const int NumChannels = AudioBuffer.getNumChannels();
    const int FdnBufferSize = delayBuffer.getNumSamples();

    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        // 1. Gather input per channel (fallback to mono if only 1 channel)
        const float InputLeft  = (NumChannels > 0) ? AudioBuffer.getWritePointer(0)[SampleIndex] : 0.0f;
        const float InputRight = (NumChannels > 1) ? AudioBuffer.getWritePointer(1)[SampleIndex] : InputLeft;

        // 2. Echo paths (stereo) with smear & advance
        float* EchoDataLeft = echoBuffer.getWritePointer(0);
        float* EchoDataRight = echoBuffer.getWritePointer(1);
        const int EchoBufferSize = echoBuffer.getNumSamples();

        // Read clean (undiffused) tap at nominal delay
        const int EchoReadPosLeftClean  = (echoWritePos[0] - echoDelaySamples + EchoBufferSize) % EchoBufferSize;
        const int EchoReadPosRightClean = (echoWritePos[1] - echoDelaySamples + EchoBufferSize) % EchoBufferSize;
        const float EchoOutLeftClean  = EchoDataLeft[EchoReadPosLeftClean];
        const float EchoOutRightClean = EchoDataRight[EchoReadPosRightClean];

        // Compute advanced (earlier) effective delay for diffused path
        const int EffectiveEchoDelaySamples = echoDelaySamples - currentAdvanceSamples;

        const int EchoReadPosLeftDiff  = (echoWritePos[0] - EffectiveEchoDelaySamples + EchoBufferSize) % EchoBufferSize;
        const int EchoReadPosRightDiff = (echoWritePos[1] - EffectiveEchoDelaySamples + EchoBufferSize) % EchoBufferSize;
        float DiffusedTapLeftRaw  = EchoDataLeft[EchoReadPosLeftDiff];
        float DiffusedTapRightRaw = EchoDataRight[EchoReadPosRightDiff];

        // Apply small allpass smear chain to earlier tap (two cascaded diffusers)
        DiffusedTapLeftRaw  = preEchoSmearLeftB.Process(preEchoSmearLeftA.Process(DiffusedTapLeftRaw));
        DiffusedTapRightRaw = preEchoSmearRightB.Process(preEchoSmearRightA.Process(DiffusedTapRightRaw));

        // Morph clean vs diffused using constant power (perceptual)
        const float CurrentDiffusionAmount = smoothedDiffusionAmount.getNextValue();
        const float CleanWeight  = std::cos(CurrentDiffusionAmount * juce::MathConstants<float>::halfPi);
        const float SmearWeight  = std::sin(CurrentDiffusionAmount * juce::MathConstants<float>::halfPi);

        const float EchoOutLeft  = EchoOutLeftClean  * CleanWeight + DiffusedTapLeftRaw  * SmearWeight;
        const float EchoOutRight = EchoOutRightClean * CleanWeight + DiffusedTapRightRaw * SmearWeight;

        // Write blended echo with feedback (feedback always sees morphed version)
        const float EchoWriteSampleLeft  = (InputLeft  + EchoOutLeft  * echoFeedbackGain);
        const float EchoWriteSampleRight = (InputRight + EchoOutRight * echoFeedbackGain);

        EchoDataLeft[echoWritePos[0]]  = EchoWriteSampleLeft;
        EchoDataRight[echoWritePos[1]] = EchoWriteSampleRight;

        echoWritePos[0] = (echoWritePos[0] + 1) % EchoBufferSize;
        echoWritePos[1] = (echoWritePos[1] + 1) % EchoBufferSize;

        // 3. Pre-FDN diffusion crossfade (secondary diffusion stage)
        // CurrentDiffusionAmount already fetched; reuse for FDN morph
        const float DiffusedEchoLeft  = diffusionStageLeft.Process(EchoOutLeft);
        const float DiffusedEchoRight = diffusionStageRight.Process(EchoOutRight);

        const float FdnInputLeft  = EchoOutLeft  + CurrentDiffusionAmount * (DiffusedEchoLeft  - EchoOutLeft);
        const float FdnInputRight = EchoOutRight + CurrentDiffusionAmount * (DiffusedEchoRight - EchoOutRight);

        // 4. FDN processing (TWO-PHASE UPDATE)

        // Phase A: Snapshot all delayed outputs BEFORE any writes
        std::array<float, numFdnChannels> Delayed{};

        for (int FdnChannelIndex = 0; FdnChannelIndex < numFdnChannels; ++FdnChannelIndex)
        {
            float* ChannelDataFdn = delayBuffer.getWritePointer(FdnChannelIndex);
            const int ReadPosition = (writePos[FdnChannelIndex] - delaySamples[FdnChannelIndex] + FdnBufferSize) % FdnBufferSize;

            Delayed[FdnChannelIndex] = ChannelDataFdn[ReadPosition];
        }

        // Phase B: Compute feedback sums using only the snapshot values
        std::array<float, numFdnChannels> FeedbackSums{};
        for (int DestChannelIndex = 0; DestChannelIndex < numFdnChannels; ++DestChannelIndex)
        {
            float Sum = 0.0f;

            for (int SrcChannelIndex = 0; SrcChannelIndex < numFdnChannels; ++SrcChannelIndex)
            {
                Sum += feedbackMatrix(DestChannelIndex, SrcChannelIndex) * Delayed[SrcChannelIndex];
            }

            FeedbackSums[DestChannelIndex] = Sum;
        }

        // HF damping (still stable because it uses FeedbackSums derived from Delayed snapshot)
        static std::array<float, numFdnChannels> feedbackLpState { 0.0f, 0.0f, 0.0f, 0.0f };
        const float lpCoeff = 0.2f;

        for (int D = 0; D < numFdnChannels; ++D)
        {
            feedbackLpState[D] = feedbackLpState[D] + lpCoeff * (FeedbackSums[D] - feedbackLpState[D]);
            FeedbackSums[D] = feedbackLpState[D];
        }

        // Phase C: Write new samples (simultaneous logical time) and advance pointers
        float FdnOutputLeft = 0.0f;
        float FdnOutputRight = 0.0f;

        for (int FdnChannelIndex = 0; FdnChannelIndex < numFdnChannels; ++FdnChannelIndex)
        {
            float* ChannelDataFdn = delayBuffer.getWritePointer(FdnChannelIndex);

            const float InputSum =
                inputMixLeft[FdnChannelIndex]  * FdnInputLeft +
                inputMixRight[FdnChannelIndex] * FdnInputRight;

            const float NewSample =
                InputSum + feedbackGains[FdnChannelIndex] * FeedbackSums[FdnChannelIndex];

            ChannelDataFdn[writePos[FdnChannelIndex]] = NewSample;
            writePos[FdnChannelIndex] = (writePos[FdnChannelIndex] + 1) % FdnBufferSize;

            // Stereo output mix uses the snapshot values to avoid instantaneous feedback coupling
            FdnOutputLeft  += outputMixLeft[FdnChannelIndex]  * Delayed[FdnChannelIndex];
            FdnOutputRight += outputMixRight[FdnChannelIndex] * Delayed[FdnChannelIndex];
        }

        // 5. Wet morph: pure delay ↔ reverb (per channel)
        const float CombinedWetLeft =
              (1.0f - CurrentDiffusionAmount) * EchoOutLeft
            + CurrentDiffusionAmount * FdnOutputLeft;

        const float CombinedWetRight =
              (1.0f - CurrentDiffusionAmount) * EchoOutRight
            + CurrentDiffusionAmount * FdnOutputRight;

        // 6. Wet/dry mix and write per channel
        const float CurrentWetDry = smoothedWetDry.getNextValue();

        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            float* ChannelData = AudioBuffer.getWritePointer(ChannelIndex);
            const float DrySample = ChannelData[SampleIndex];

            const float WetSample = (ChannelIndex % 2 == 0) ? CombinedWetLeft : CombinedWetRight;

            ChannelData[SampleIndex] = DrySample * (1.0f - CurrentWetDry)
                                     + WetSample * CurrentWetDry;
        }
    }
}

//==============================================================================
void DiffusedDelayReverb::UpdateSmearAndAdvance()
{
    // Advance scales with diffusionAmount: more diffusion => earlier perceived tap
    const float Diffusion = diffusionAmount; // already clamped 0..1

    const float AdvanceMs = Diffusion * maxSmearAdvanceMs;
    currentAdvanceSamples = static_cast<int>(AdvanceMs * 0.001f * sampleRate);

    // Ensure we never eliminate the entire delay
    currentAdvanceSamples = juce::jlimit(0,
                                         juce::jmax(0, echoDelaySamples - 4), // keep >= 4 samples residual
                                         currentAdvanceSamples);

    // Set diffuser delays relative to diffusionQuality / diffusionSize if desired.
    // Simple mapping: shorter and varied for low quality, longer for high.
    const float Quality = diffusionQuality;
    const float Size = diffusionSize;

    // Base small smear delays (ms)
    const float BaseAms = 3.0f + Size * 4.0f;   // 3–7 ms
    const float BaseBms = 2.0f + Quality * 5.0f; // 2–7 ms (decorrelate)

    int DelayA = static_cast<int>(BaseAms * 0.001f * sampleRate);
    int DelayB = static_cast<int>(BaseBms * 0.001f * sampleRate);

    // Vary left/right slightly (pseudo prime offsets)
    preEchoSmearLeftA.SetDelaySamples(DelayA);
    preEchoSmearLeftB.SetDelaySamples(DelayB);
    preEchoSmearRightA.SetDelaySamples(DelayA + 7); // offset by 7 samples
    preEchoSmearRightB.SetDelaySamples(juce::jmax(1, DelayB - 5));
}

void DiffusedDelayReverb::UpdateDelayBuffer()
{
    const int BufferSize = delayBuffer.getNumSamples();

    if (BufferSize == 0)
        return;

    // Keep FDN delays largely independent of user delayTimeSeconds
    // so that shrinking the user delay does not collapse FDN structure.
    // We use a mild scaling (sqrt) and a floor to avoid identical lengths.
    const std::array<int, numFdnChannels> basePrimesMs = { 29, 37, 41, 53 };

    // Mild scaling in 0.5s reference space
    const float Normalized = juce::jlimit(0.001f, maxDelayTimeSeconds, delayTimeSeconds);
    const float Scale = std::sqrt(Normalized / 0.5f); // Gentle scaling (sub-linear)
    const float MinDelayMs = 8.0f;                    // Floor to keep diversity

    for (int ChannelIndex = 0; ChannelIndex < numFdnChannels; ++ChannelIndex)
    {
        const float TargetMs = juce::jmax(MinDelayMs, basePrimesMs[ChannelIndex] * Scale);
        const int TargetSamples = static_cast<int>(TargetMs * 0.001f * sampleRate);

        // Keep a minimum length > 10 but allow distinct lengths
        delaySamples[ChannelIndex] = juce::jlimit(12, BufferSize - 1, TargetSamples);

        const float DelayTimeInSeconds = static_cast<float>(delaySamples[ChannelIndex]) / sampleRate;

        // Per-line decay gain (RT60 = feedbackTimeSeconds)
        const float Rt60 = juce::jmax(0.05f, feedbackTimeSeconds); // Enforce minimum RT60 (avoid near-unity for tiny lines with huge RT60)
        feedbackGains[ChannelIndex] = std::pow(0.001f, DelayTimeInSeconds / Rt60);

        // Safety cap for extremely short lines
        if (delaySamples[ChannelIndex] < 25)
            feedbackGains[ChannelIndex] = juce::jmin(feedbackGains[ChannelIndex], 0.995f);
    }

    UpdateFeedbackMatrix();
    UpdateEchoSettings();
}

void DiffusedDelayReverb::UpdateEchoSettings()
{
    const int EchoBufferSamples = echoBuffer.getNumSamples();

    if (EchoBufferSamples <= 1)
        return;

    echoDelaySamples = juce::jlimit(1,
                                    EchoBufferSamples - 1,
                                    static_cast<int>(std::round(delayTimeSeconds * sampleRate)));

    const float EchoDelaySeconds = static_cast<float>(echoDelaySamples) / sampleRate;

    if (feedbackTimeSeconds <= 0.0001f)
    {
        echoFeedbackGain = 0.0f;
        return;
    }

    // RT60 mapping
    echoFeedbackGain = std::pow(0.001f, EchoDelaySeconds / feedbackTimeSeconds);

    // Heuristic attenuation for very short delays (avoid metallic runaway)
    if (EchoDelaySeconds < 0.02f)          // < 20 ms
    {
        // Blend toward a lower max (0.90–0.95) to keep repeats but stop fizz
        const float ShortBlend = 1.0f - (EchoDelaySeconds / 0.02f); // 1 at 0s, 0 at 20ms
        const float MaxShortGain = 0.94f;  // ceiling for very short echoes

        echoFeedbackGain = echoFeedbackGain * (1.0f - ShortBlend) + MaxShortGain * ShortBlend;
        echoFeedbackGain = juce::jmin(echoFeedbackGain, MaxShortGain);
    }

    UpdateSmearAndAdvance();
}

void DiffusedDelayReverb::UpdateFeedbackMatrix()
{
    const float s = 1.0f / std::sqrt(static_cast<float>(numFdnChannels));  // 0.5 for N=4
    feedbackMatrix(0, 0) =  s; feedbackMatrix(0, 1) =  s; feedbackMatrix(0, 2) =  s; feedbackMatrix(0, 3) =  s;
    feedbackMatrix(1, 0) =  s; feedbackMatrix(1, 1) =  s; feedbackMatrix(1, 2) = -s; feedbackMatrix(1, 3) = -s;
    feedbackMatrix(2, 0) =  s; feedbackMatrix(2, 1) = -s; feedbackMatrix(2, 2) =  s; feedbackMatrix(2, 3) = -s;
    feedbackMatrix(3, 0) =  s; feedbackMatrix(3, 1) = -s; feedbackMatrix(3, 2) = -s; feedbackMatrix(3, 3) =  s;
}

void DiffusedDelayReverb::UpdateStereoMixMatrices()
{
    // Distribute left input into FDN channels 0 and 2; right input into channels 1 and 3
    inputMixLeft  = { 0.5f, 0.0f, 0.5f, 0.0f };
    inputMixRight = { 0.0f, 0.5f, 0.0f, 0.5f };

    // Decorrelated stereo output from the 4 FDN lines
    outputMixLeft  = {  0.5f, -0.5f,  0.5f, -0.5f };
    outputMixRight = { -0.5f,  0.5f, -0.5f,  0.5f };
}

float DiffusedDelayReverb::ProcessFDNChannel(int ChannelIndex, float InputSample)
{
    // ----- 1. Read delayed sample -----
    float* ChannelData = delayBuffer.getWritePointer(ChannelIndex);
    const int BufferSize = delayBuffer.getNumSamples();
    const int ReadPosition = (writePos[ChannelIndex] - delaySamples[ChannelIndex] + BufferSize) % BufferSize;
    const float DelayedSample = ChannelData[ReadPosition];

    // ----- 2. Gather feedback from every other line -----
    float FeedbackSum = 0.0f;

    for (int SourceChannel = 0; SourceChannel < numFdnChannels; ++SourceChannel)
    {
        float* SourceData = delayBuffer.getWritePointer(SourceChannel);
        const int SourceReadPos = (writePos[SourceChannel] - delaySamples[SourceChannel] + BufferSize) % BufferSize;
        FeedbackSum += feedbackMatrix(ChannelIndex, SourceChannel) * SourceData[SourceReadPos];
    }

    // ----- 3. Apply per-line attenuation (60 dB decay) -----
    const float AttenuatedFeedback = FeedbackSum * feedbackGains[ChannelIndex];

    // ----- 4. Write new sample (input + feedback) -----
    const float NewSample = InputSample + AttenuatedFeedback;
    ChannelData[writePos[ChannelIndex]] = NewSample;

    // ----- 5. Return the delayed sample (what the mixer hears) -----
    return DelayedSample;
}

//==============================================================================
void DiffusedDelayReverb::AllpassDiffuser::Prepare(float sr, int maxDelaySamples)
{
    sampleRate = sr;
    const int size = juce::nextPowerOfTwo(maxDelaySamples);
    buffer.setSize(1, size, false, true, false);
    buffer.clear();
    writePos = 0;
}

void DiffusedDelayReverb::AllpassDiffuser::SetDelaySamples(int samples)
{
    delaySamples = juce::jlimit(1, buffer.getNumSamples() - 1, samples);
}

float DiffusedDelayReverb::AllpassDiffuser::Process(float input)
{
    float* data = buffer.getWritePointer(0);
    const int bufferSize = buffer.getNumSamples();
    const int readPos = (writePos - delaySamples + bufferSize) % bufferSize;

    const float delayed = data[readPos];
    const float output = -input * 0.5f + delayed * 0.5f;
    data[writePos] = input + delayed * 0.5f;

    writePos = (writePos + 1) % bufferSize;
    return output;
}

//==============================================================================
void DiffusedDelayReverb::DiffusionStage::Prepare(float SampleRate, float Size, float Quality)
{
    sampleRate = SampleRate;
    currentSize = Size;
    currentQuality = Quality;

    // Allocate enough headroom in samples based on sampleRate (about 250 ms max)
    const float MaxDelayMs = 250.0f;
    const int MaxSamples = static_cast<int>((MaxDelayMs * 0.001f) * sampleRate);

    // ---- SERIES DIFFUSERS -------------------------------------------------
    numSeries = 1 + static_cast<int>(Quality * 3);  // 1 to 4
    seriesDiffusers.clear();
    seriesDiffusers.shrink_to_fit();
    seriesDiffusers.reserve(numSeries);

    for (int Index = 0; Index < numSeries; ++Index)
    {
        seriesDiffusers.emplace_back();
        seriesDiffusers.back().Prepare(sampleRate, MaxSamples);
    }

    // ---- PARALLEL DIFFUSERS -----------------------------------------------
    numParallel = 2 + static_cast<int>(Quality * 4);  // 2 to 6
    parallelDiffusers.clear();
    parallelDiffusers.shrink_to_fit();
    parallelDiffusers.reserve(numParallel);

    for (int Index = 0; Index < numParallel; ++Index)
    {
        parallelDiffusers.emplace_back();
        parallelDiffusers.back().Prepare(sampleRate, MaxSamples);
    }

    UpdateParameters(Size, Quality);
}

void DiffusedDelayReverb::DiffusionStage::UpdateParameters(float size, float quality)
{
    currentSize = size;
    currentQuality = quality;

    const float baseSizeMs = 5.0f + size * 40.0f; // 5 to 45 ms
    const int baseSamples = static_cast<int>(baseSizeMs * 0.001f * sampleRate);

    // Prime offsets for incommensurate delays
    const std::vector<int> primes = { 2, 3, 5, 7, 11, 13, 17, 19, 23 };

    // Series chain
    for (int i = 0; i < static_cast<int>(seriesDiffusers.size()); ++i)
    {
        const int offset = primes[static_cast<size_t>(i) % primes.size()];
        const int samples = baseSamples * (i + 1) * offset / 7;
        seriesDiffusers[static_cast<size_t>(i)].SetDelaySamples(samples);
    }

    // Parallel bank
    for (int i = 0; i < static_cast<int>(parallelDiffusers.size()); ++i)
    {
        const int offset = primes[(static_cast<size_t>(i) + 3) % primes.size()];
        const int samples = baseSamples * offset / 5;
        parallelDiffusers[static_cast<size_t>(i)].SetDelaySamples(samples);
    }
}

float DiffusedDelayReverb::DiffusionStage::Process(float input)
{
    float out = input;

    // Series diffusion
    for (auto& d : seriesDiffusers)
    {
        out = d.Process(out);
    }

    // Parallel diffusion (sum)
    float parallelSum = 0.0f;

    for (auto& d : parallelDiffusers)
    {
        parallelSum += d.Process(out);
    }

    parallelSum /= static_cast<float>(parallelDiffusers.size());

    // Blend series and parallel
    const float blend = currentQuality;
    return out * (1.0f - blend) + parallelSum * blend;
}

//==============================================================================
void DiffusedDelayReverb::UpdateDiffusionNetwork()
{
    diffusionStageLeft.UpdateParameters(diffusionSize, diffusionQuality);
    diffusionStageRight.UpdateParameters(diffusionSize, diffusionQuality);
}