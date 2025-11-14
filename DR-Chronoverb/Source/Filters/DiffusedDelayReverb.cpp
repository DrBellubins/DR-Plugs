#include "DiffusedDelayReverb.h"

//==============================================================================
DiffusedDelayReverb::DiffusedDelayReverb()
    : feedbackMatrix(numFdnChannels, numFdnChannels)
{
    UpdateFeedbackMatrix();

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

    // Resize dedicated echo buffer (mono/stereo)
    const int MaxEchoSamples = juce::nextPowerOfTwo(static_cast<int>(maxDelayTimeSeconds * sampleRate) + 1);
    echoBuffer.setSize(2, MaxEchoSamples, false, true, false);
    echoBuffer.clear();
    echoWritePos = { 0, 0 };

    // Keep inputBuffer (not used for echo now), clear it
    const int MaxInputSamples = juce::nextPowerOfTwo(static_cast<int>(maxPreDelayMs * 0.001f * sampleRate) + FdnBufferSize);
    inputBuffer.setSize(1, MaxInputSamples, false, true, false);
    inputBuffer.clear();
    inputWritePos = 0;

    // Update FDN delays
    UpdateDelayBuffer();

    // Prepare diffusion stage
    diffusionStage.Prepare(sampleRate, diffusionSize, diffusionQuality);

    // Reset smoothing
    smoothedDiffusionAmount.setCurrentAndTargetValue(diffusionAmount);
    smoothedWetDry.setCurrentAndTargetValue(wetDryMix);
}

//==============================================================================
void DiffusedDelayReverb::SetDelayTime(float timeSeconds)
{
    delayTimeSeconds = juce::jlimit(0.001f, maxDelayTimeSeconds, timeSeconds);
    UpdateDelayBuffer();
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
}

void DiffusedDelayReverb::SetDiffusionSize(float size)
{
    diffusionSize = juce::jlimit(0.0f, 1.0f, size);
    UpdateDiffusionNetwork();
}

void DiffusedDelayReverb::SetDiffusionQuality(float quality)
{
    diffusionQuality = juce::jlimit(0.0f, 1.0f, quality);

    // Rebuild the diffusion topology so numSeries/numParallel and buffers update with quality
    diffusionStage.Prepare(sampleRate, diffusionSize, diffusionQuality);
}

void DiffusedDelayReverb::SetDryWetMix(float mix)
{
    wetDryMix = juce::jlimit(0.0f, 1.0f, mix);
    smoothedWetDry.setTargetValue(wetDryMix);
}

//==============================================================================
void DiffusedDelayReverb::ProcessBlock(juce::AudioBuffer<float>& AudioBuffer)
{
    const int NumSamples = AudioBuffer.getNumSamples();
    const int NumChannels = AudioBuffer.getNumChannels();

    for (int Channel = 0; Channel < NumChannels; ++Channel)
    {
        float* ChannelData = AudioBuffer.getWritePointer(Channel);

        // Echo buffer pointers
        float* EchoData = echoBuffer.getWritePointer(juce::jmin(Channel, echoBuffer.getNumChannels() - 1));
        const int EchoBufferSize = echoBuffer.getNumSamples();

        for (int Sample = 0; Sample < NumSamples; ++Sample)
        {
            const float InputSample = ChannelData[Sample];

            // ===================== Echo/Delay Path =====================
            const int EchoReadPos = (echoWritePos[Channel] - echoDelaySamples + EchoBufferSize) % EchoBufferSize;
            const float EchoOut = EchoData[EchoReadPos];

            // Feedback write: input + feedback * previous echo
            const float EchoWriteSample = InputSample + EchoOut * echoFeedbackGain;
            EchoData[echoWritePos[Channel]] = EchoWriteSample;

            // Advance echo write position
            echoWritePos[Channel] = (echoWritePos[Channel] + 1) % EchoBufferSize;

            // ===================== Pre-FDN Diffusion Crossfade =====================
            const float CurrentDiffusionAmount = smoothedDiffusionAmount.getNextValue();
            const float Diffused = diffusionStage.Process(EchoOut);
            const float FdnInput = EchoOut + CurrentDiffusionAmount * (Diffused - EchoOut);
            const float PerChannelInput = FdnInput / static_cast<float>(numFdnChannels);

            // ===================== FDN Processing (all lines per sample) =====================
            float FdnOutputSum = 0.0f;

            for (int FdnChannel = 0; FdnChannel < numFdnChannels; ++FdnChannel)
            {
                float* ChannelDataFdn = delayBuffer.getWritePointer(FdnChannel);
                const int BufferSize = delayBuffer.getNumSamples();

                // Read delayed sample
                const int ReadPosition = (writePos[FdnChannel] - delaySamples[FdnChannel] + BufferSize) % BufferSize;
                const float DelayedSample = ChannelDataFdn[ReadPosition];

                // Compute feedback from all lines
                float FeedbackSum = 0.0f;

                for (int Source = 0; Source < numFdnChannels; ++Source)
                {
                    float* SrcData = delayBuffer.getWritePointer(Source);
                    const int SrcReadPos = (writePos[Source] - delaySamples[Source] + BufferSize) % BufferSize;
                    FeedbackSum += feedbackMatrix(FdnChannel, Source) * SrcData[SrcReadPos];
                }

                const float AttenuatedFeedback = FeedbackSum * feedbackGains[FdnChannel];
                const float NewSample = PerChannelInput + AttenuatedFeedback;

                // Write new sample
                ChannelDataFdn[writePos[FdnChannel]] = NewSample;

                // Advance write position
                writePos[FdnChannel] = (writePos[FdnChannel] + 1) % BufferSize;

                FdnOutputSum += DelayedSample;
            }

            // ===================== Wet Mix: Delay ↔ Reverb Morph =====================
            const float CombinedWet = (1.0f - CurrentDiffusionAmount) * EchoOut
                                    + (CurrentDiffusionAmount) * FdnOutputSum;

            // ===================== Final Wet/Dry =====================
            const float CurrentWetDry = smoothedWetDry.getNextValue();
            const float OutputSample = InputSample * (1.0f - CurrentWetDry)
                                     + CombinedWet * CurrentWetDry;

            ChannelData[Sample] = OutputSample;
        }
    }
}

//==============================================================================
void DiffusedDelayReverb::UpdateDelayBuffer()
{
    const int BufferSize = delayBuffer.getNumSamples();

    if (BufferSize == 0)
    {
        return;
    }

    // Always use short prime-based delays and a proper scattering feedback matrix.
    // Decouple from diffusionAmount so Amount can be changed at runtime without topology switches.
    const std::array<int, numFdnChannels> basePrimes = { 29, 37, 41, 53 };
    const float scale = delayTimeSeconds / 0.5f; // Normalize around ~500 ms

    for (int Index = 0; Index < numFdnChannels; ++Index)
    {
        const int baseMs = basePrimes[Index];
        const float targetMs = baseMs * scale;

        delaySamples[Index] = juce::jlimit(10, BufferSize - 1, static_cast<int>(targetMs * 0.001f * sampleRate));

        const float DelayTimeInSeconds = static_cast<float>(delaySamples[Index]) / sampleRate;

        // 60 dB decay time per line = feedbackTimeSeconds
        feedbackGains[Index] = std::pow(0.001f, DelayTimeInSeconds / juce::jmax(0.001f, feedbackTimeSeconds));
    }

    UpdateFeedbackMatrix(); // Use Hadamard-like fixed scattering

    // Update classic echo settings (uses main delay time and feedback time)
    UpdateEchoSettings();
}

void DiffusedDelayReverb::UpdateEchoSettings()
{
    if (echoBuffer.getNumSamples() <= 1)
    {
        return;
    }

    echoDelaySamples = juce::jlimit(1, echoBuffer.getNumSamples() - 1,
                                    static_cast<int>(std::round(delayTimeSeconds * sampleRate)));

    const float EchoDelaySeconds = static_cast<float>(echoDelaySamples) / sampleRate;

    if (feedbackTimeSeconds <= 0.0001f)
    {
        echoFeedbackGain = 0.0f;
    }
    else
    {
        // 60 dB decay over 'feedbackTimeSeconds'
        echoFeedbackGain = std::pow(0.001f, EchoDelaySeconds / feedbackTimeSeconds);
    }
}

void DiffusedDelayReverb::UpdateFeedbackMatrix()
{
    const float s = 1.0f / std::sqrt(static_cast<float>(numFdnChannels));  // 0.5 for N=4
    feedbackMatrix(0, 0) =  s; feedbackMatrix(0, 1) =  s; feedbackMatrix(0, 2) =  s; feedbackMatrix(0, 3) =  s;
    feedbackMatrix(1, 0) =  s; feedbackMatrix(1, 1) =  s; feedbackMatrix(1, 2) = -s; feedbackMatrix(1, 3) = -s;
    feedbackMatrix(2, 0) =  s; feedbackMatrix(2, 1) = -s; feedbackMatrix(2, 2) =  s; feedbackMatrix(2, 3) = -s;
    feedbackMatrix(3, 0) =  s; feedbackMatrix(3, 1) = -s; feedbackMatrix(3, 2) = -s; feedbackMatrix(3, 3) =  s;
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
    diffusionStage.UpdateParameters(diffusionSize, diffusionQuality);
}