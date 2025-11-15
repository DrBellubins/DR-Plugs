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

    smearDiffusersReady = true;
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
    if (smearDiffusersReady)
    {
        UpdateSmearAndAdvance();
    }
}

void DiffusedDelayReverb::SetFeedbackTime(float feedback)
{
    feedbackTimeSeconds = feedback;
    UpdateDelayBuffer();
    if (smearDiffusersReady)
    {
        UpdateSmearAndAdvance();
    }
}

void DiffusedDelayReverb::SetDiffusionAmount(float amount)
{
    diffusionAmount = juce::jlimit(0.0f, 1.0f, amount);
    smoothedDiffusionAmount.setTargetValue(diffusionAmount);
    if (smearDiffusersReady)
    {
        UpdateSmearAndAdvance();
    }
}

void DiffusedDelayReverb::SetDiffusionSize(float size)
{
    diffusionSize = juce::jlimit(0.0f, 1.0f, size);
    UpdateDiffusionNetwork();
    if (smearDiffusersReady)
    {
        UpdateSmearAndAdvance();
    }
}

void DiffusedDelayReverb::SetDiffusionQuality(float quality)
{
    diffusionQuality = juce::jlimit(0.0f, 1.0f, quality);
    diffusionStageLeft.Prepare(sampleRate, diffusionSize, diffusionQuality);
    diffusionStageRight.Prepare(sampleRate, diffusionSize, diffusionQuality);
    if (smearDiffusersReady)
    {
        UpdateSmearAndAdvance();
    }
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

        // 2. Echo paths (stereo) with smear & fractional advance
        float* EchoDataLeft = echoBuffer.getWritePointer(0);
        float* EchoDataRight = echoBuffer.getWritePointer(1);
        const int EchoBufferSize = echoBuffer.getNumSamples();

        // Read clean (undiffused) tap at nominal integer delay
        const int EchoReadPosLeftClean  = (echoWritePos[0] - echoDelaySamples + EchoBufferSize) % EchoBufferSize;
        const int EchoReadPosRightClean = (echoWritePos[1] - echoDelaySamples + EchoBufferSize) % EchoBufferSize;
        const float EchoOutLeftClean  = EchoDataLeft[EchoReadPosLeftClean];
        const float EchoOutRightClean = EchoDataRight[EchoReadPosRightClean];

        // Obtain smoothed diffusion for this sample
        const float CurrentDiffusionAmount = smoothedDiffusionAmount.getNextValue();

        // Fractional advance (earlier onset) derived from diffusion amount
        const float AdvanceSamplesFloat = CurrentDiffusionAmount * (maxSmearAdvanceMs * 0.001f * sampleRate);

        // Effective fractional delay (never let it collapse below 4 samples to avoid zero/negative)
        float EffectiveDelayFloat = static_cast<float>(echoDelaySamples) - AdvanceSamplesFloat;
        if (EffectiveDelayFloat < 4.0f)
        {
            EffectiveDelayFloat = 4.0f;
        }

        // Compute float read indices (write position points to newest sample to be written next)
        float ReadIndexLeftFloat  = static_cast<float>(echoWritePos[0]) - EffectiveDelayFloat;
        float ReadIndexRightFloat = static_cast<float>(echoWritePos[1]) - EffectiveDelayFloat;

        // Wrap float indices into buffer range (positive modulo)
        auto WrapFloatIndex = [EchoBufferSize](float Index) -> float
        {
            const float BufferSizeFloat = static_cast<float>(EchoBufferSize);

            while (Index < 0.0f)
            {
                Index += BufferSizeFloat;
            }
            while (Index >= BufferSizeFloat)
            {
                Index -= BufferSizeFloat;
            }
            return Index;
        };

        ReadIndexLeftFloat  = WrapFloatIndex(ReadIndexLeftFloat);
        ReadIndexRightFloat = WrapFloatIndex(ReadIndexRightFloat);

        // Integer base indices and fractional part
        const int IndexLeft0  = static_cast<int>(ReadIndexLeftFloat);
        const int IndexRight0 = static_cast<int>(ReadIndexRightFloat);
        const int IndexLeft1  = (IndexLeft0 + 1) % EchoBufferSize;
        const int IndexRight1 = (IndexRight0 + 1) % EchoBufferSize;

        const float FractionLeft  = ReadIndexLeftFloat  - static_cast<float>(IndexLeft0);
        const float FractionRight = ReadIndexRightFloat - static_cast<float>(IndexRight0);

        // Linear interpolation for diffused (advanced) raw taps
        float DiffusedTapLeftRaw  = EchoDataLeft[IndexLeft0]  * (1.0f - FractionLeft)  + EchoDataLeft[IndexLeft1]  * FractionLeft;
        float DiffusedTapRightRaw = EchoDataRight[IndexRight0] * (1.0f - FractionRight) + EchoDataRight[IndexRight1] * FractionRight;

        // Apply small allpass smear chain to earlier tap (two cascaded diffusers) if ready
        if (smearDiffusersReady)
        {
            DiffusedTapLeftRaw  = preEchoSmearLeftB.Process(preEchoSmearLeftA.Process(DiffusedTapLeftRaw));
            DiffusedTapRightRaw = preEchoSmearRightB.Process(preEchoSmearRightA.Process(DiffusedTapRightRaw));
        }

        // Morph clean vs diffused using constant power (perceptual smooth loudness)
        const float CleanWeight = std::cos(CurrentDiffusionAmount * juce::MathConstants<float>::halfPi);
        const float SmearWeight = std::sin(CurrentDiffusionAmount * juce::MathConstants<float>::halfPi);

        const float EchoOutLeft  = EchoOutLeftClean  * CleanWeight + DiffusedTapLeftRaw  * SmearWeight;
        const float EchoOutRight = EchoOutRightClean * CleanWeight + DiffusedTapRightRaw * SmearWeight;

        // Write blended echo with feedback (feedback always sees morphed version)
        const float EchoWriteSampleLeft  = InputLeft  + EchoOutLeft  * echoFeedbackGain;
        const float EchoWriteSampleRight = InputRight + EchoOutRight * echoFeedbackGain;

        EchoDataLeft[echoWritePos[0]]  = EchoWriteSampleLeft;
        EchoDataRight[echoWritePos[1]] = EchoWriteSampleRight;

        echoWritePos[0] = (echoWritePos[0] + 1) % EchoBufferSize;
        echoWritePos[1] = (echoWritePos[1] + 1) % EchoBufferSize;

        // 3. Pre-FDN diffusion crossfade (secondary diffusion stage)
        const float DiffusedEchoLeft  = diffusionStageLeft.Process(EchoOutLeft);
        const float DiffusedEchoRight = diffusionStageRight.Process(EchoOutRight);

        const float FdnInputLeft  = EchoOutLeft  + CurrentDiffusionAmount * (DiffusedEchoLeft  - EchoOutLeft);
        const float FdnInputRight = EchoOutRight + CurrentDiffusionAmount * (DiffusedEchoRight - EchoOutRight);

        // 4. FDN processing (two-phase update)
        std::array<float, numFdnChannels> Delayed{};

        for (int FdnChannelIndex = 0; FdnChannelIndex < numFdnChannels; ++FdnChannelIndex)
        {
            float* ChannelDataFdn = delayBuffer.getWritePointer(FdnChannelIndex);
            const int ReadPosition = (writePos[FdnChannelIndex] - delaySamples[FdnChannelIndex] + FdnBufferSize) % FdnBufferSize;
            Delayed[FdnChannelIndex] = ChannelDataFdn[ReadPosition];
        }

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

        static std::array<float, numFdnChannels> feedbackLpState { 0.0f, 0.0f, 0.0f, 0.0f };
        const float lpCoeff = 0.2f;

        for (int D = 0; D < numFdnChannels; ++D)
        {
            feedbackLpState[D] = feedbackLpState[D] + lpCoeff * (FeedbackSums[D] - feedbackLpState[D]);
            FeedbackSums[D] = feedbackLpState[D];
        }

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
    if (!smearDiffusersReady)
    {
        return;
    }

    // Integer advance (legacy / guard for min residual delay)
    const float Diffusion = diffusionAmount;
    const float AdvanceMs = Diffusion * maxSmearAdvanceMs;
    currentAdvanceSamples = static_cast<int>(AdvanceMs * 0.001f * sampleRate);

    currentAdvanceSamples = juce::jlimit(0,
                                         juce::jmax(0, echoDelaySamples - 4),
                                         currentAdvanceSamples);

    // Set diffuser delays relative to diffusionQuality / diffusionSize
    const float Quality = diffusionQuality;
    const float Size = diffusionSize;

    const float BaseAms = 3.0f + Size * 4.0f;    // 3–7 ms
    const float BaseBms = 2.0f + Quality * 5.0f; // 2–7 ms

    int DelayA = static_cast<int>(BaseAms * 0.001f * sampleRate);
    int DelayB = static_cast<int>(BaseBms * 0.001f * sampleRate);

    DelayA = juce::jmax(1, DelayA);
    DelayB = juce::jmax(1, DelayB);

    const int MaxA = preEchoSmearLeftA.getAllocatedSize() - 2;
    const int MaxB = preEchoSmearLeftB.getAllocatedSize() - 2;

    if (MaxA > 1)
    {
        DelayA = juce::jmin(DelayA, MaxA);
    }
    if (MaxB > 1)
    {
        DelayB = juce::jmin(DelayB, MaxB);
    }

    preEchoSmearLeftA.SetDelaySamples(DelayA);
    preEchoSmearLeftB.SetDelaySamples(DelayB);

    preEchoSmearRightA.SetDelaySamples(juce::jmin(DelayA + 7, (MaxA > 1) ? MaxA : DelayA));
    preEchoSmearRightB.SetDelaySamples(juce::jmax(1, juce::jmin(DelayB - 5, (MaxB > 1) ? MaxB : DelayB)));
}

void DiffusedDelayReverb::UpdateDelayBuffer()
{
    const int BufferSize = delayBuffer.getNumSamples();

    if (BufferSize == 0)
    {
        return;
    }

    const std::array<int, numFdnChannels> basePrimesMs = { 29, 37, 41, 53 };

    const float Normalized = juce::jlimit(0.001f, maxDelayTimeSeconds, delayTimeSeconds);
    const float Scale = std::sqrt(Normalized / 0.5f);
    const float MinDelayMsLocal = 8.0f;

    for (int ChannelIndex = 0; ChannelIndex < numFdnChannels; ++ChannelIndex)
    {
        const float TargetMs = juce::jmax(MinDelayMsLocal, basePrimesMs[ChannelIndex] * Scale);
        const int TargetSamples = static_cast<int>(TargetMs * 0.001f * sampleRate);

        delaySamples[ChannelIndex] = juce::jlimit(12, BufferSize - 1, TargetSamples);

        const float DelayTimeInSeconds = static_cast<float>(delaySamples[ChannelIndex]) / sampleRate;

        const float Rt60 = juce::jmax(0.05f, feedbackTimeSeconds);
        feedbackGains[ChannelIndex] = std::pow(0.001f, DelayTimeInSeconds / Rt60);

        if (delaySamples[ChannelIndex] < 25)
        {
            feedbackGains[ChannelIndex] = juce::jmin(feedbackGains[ChannelIndex], 0.995f);
        }
    }

    UpdateFeedbackMatrix();
    UpdateEchoSettings();
}

void DiffusedDelayReverb::UpdateEchoSettings()
{
    const int EchoBufferSamples = echoBuffer.getNumSamples();

    if (EchoBufferSamples <= 1)
    {
        return;
    }

    echoDelaySamples = juce::jlimit(1,
                                    EchoBufferSamples - 1,
                                    static_cast<int>(std::round(delayTimeSeconds * sampleRate)));

    const float EchoDelaySeconds = static_cast<float>(echoDelaySamples) / sampleRate;

    if (feedbackTimeSeconds <= 0.0001f)
    {
        echoFeedbackGain = 0.0f;
        UpdateSmearAndAdvance();
        return;
    }

    echoFeedbackGain = std::pow(0.001f, EchoDelaySeconds / feedbackTimeSeconds);

    if (EchoDelaySeconds < 0.02f)
    {
        const float ShortBlend = 1.0f - (EchoDelaySeconds / 0.02f);
        const float MaxShortGain = 0.94f;

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
    inputMixLeft  = { 0.5f, 0.0f, 0.5f, 0.0f };
    inputMixRight = { 0.0f, 0.5f, 0.0f, 0.5f };

    outputMixLeft  = {  0.5f, -0.5f,  0.5f, -0.5f };
    outputMixRight = { -0.5f,  0.5f, -0.5f,  0.5f };
}

float DiffusedDelayReverb::ProcessFDNChannel(int ChannelIndex, float InputSample)
{
    float* ChannelData = delayBuffer.getWritePointer(ChannelIndex);
    const int BufferSize = delayBuffer.getNumSamples();
    const int ReadPosition = (writePos[ChannelIndex] - delaySamples[ChannelIndex] + BufferSize) % BufferSize;
    const float DelayedSample = ChannelData[ReadPosition];

    float FeedbackSum = 0.0f;

    for (int SourceChannel = 0; SourceChannel < numFdnChannels; ++SourceChannel)
    {
        float* SourceData = delayBuffer.getWritePointer(SourceChannel);
        const int SourceReadPos = (writePos[SourceChannel] - delaySamples[SourceChannel] + BufferSize) % BufferSize;
        FeedbackSum += feedbackMatrix(ChannelIndex, SourceChannel) * SourceData[SourceReadPos];
    }

    const float AttenuatedFeedback = FeedbackSum * feedbackGains[ChannelIndex];
    const float NewSample = InputSample + AttenuatedFeedback;
    ChannelData[writePos[ChannelIndex]] = NewSample;

    return DelayedSample;
}

//==============================================================================
void DiffusedDelayReverb::AllpassDiffuser::Prepare(float sr, int maxDelaySamples)
{
    sampleRate = sr;
    const int size = juce::nextPowerOfTwo(juce::jmax(2, maxDelaySamples));
    buffer.setSize(1, size, false, true, false);
    buffer.clear();
    writePos = 0;
    delaySamples = juce::jmin(1, size - 1);
}

void DiffusedDelayReverb::AllpassDiffuser::SetDelaySamples(int samples)
{
    const int BufferSamples = buffer.getNumSamples();

    if (BufferSamples < 2)
    {
        delaySamples = 0;
        return;
    }

    delaySamples = juce::jlimit(1, BufferSamples - 1, samples);
}

float DiffusedDelayReverb::AllpassDiffuser::Process(float input)
{
    float* data = buffer.getWritePointer(0);
    const int bufferSize = buffer.getNumSamples();

    if (bufferSize < 2 || delaySamples <= 0)
    {
        return input;
    }

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

    const float MaxDelayMs = 250.0f;
    const int MaxSamples = static_cast<int>((MaxDelayMs * 0.001f) * sampleRate);

    numSeries = 1 + static_cast<int>(Quality * 3);  // 1 to 4
    seriesDiffusers.clear();
    seriesDiffusers.shrink_to_fit();
    seriesDiffusers.reserve(numSeries);

    for (int Index = 0; Index < numSeries; ++Index)
    {
        seriesDiffusers.emplace_back();
        seriesDiffusers.back().Prepare(sampleRate, MaxSamples);
    }

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

    const std::vector<int> primes = { 2, 3, 5, 7, 11, 13, 17, 19, 23 };

    for (int i = 0; i < static_cast<int>(seriesDiffusers.size()); ++i)
    {
        const int offset = primes[static_cast<size_t>(i) % primes.size()];
        const int samples = baseSamples * (i + 1) * offset / 7;
        seriesDiffusers[static_cast<size_t>(i)].SetDelaySamples(samples);
    }

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

    for (auto& d : seriesDiffusers)
    {
        out = d.Process(out);
    }

    float parallelSum = 0.0f;

    for (auto& d : parallelDiffusers)
    {
        parallelSum += d.Process(out);
    }

    if (!parallelDiffusers.empty())
    {
        parallelSum /= static_cast<float>(parallelDiffusers.size());
    }

    const float blend = currentQuality;
    return out * (1.0f - blend) + parallelSum * blend;
}

//==============================================================================
void DiffusedDelayReverb::UpdateDiffusionNetwork()
{
    diffusionStageLeft.UpdateParameters(diffusionSize, diffusionQuality);
    diffusionStageRight.UpdateParameters(diffusionSize, diffusionQuality);
}