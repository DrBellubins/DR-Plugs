#include "DiffusedDelayReverb.h"

//==============================================================================
DiffusedDelayReverb::DiffusedDelayReverb()
    : feedbackMatrix(numFdnChannels, numFdnChannels)
{
    UpdateFeedbackMatrix();

    // Prime delays for FDN (in milliseconds, will be scaled)
    const std::array<int, numFdnChannels> primeDelaysMs = { 29, 37, 41, 53 };

    for (int i = 0; i < numFdnChannels; ++i)
        delaySamples[i] = static_cast<int>(primeDelaysMs[i] * 0.001f * sampleRate);

    smoothedDiffusionAmount.reset(44100, 0.05f);
    smoothedWetDry.reset(44100, 0.05f);
}

//==============================================================================
void DiffusedDelayReverb::PrepareToPlay(double newSampleRate, float maxDelaySeconds)
{
    sampleRate = static_cast<float>(newSampleRate);
    maxDelayTimeSeconds = maxDelaySeconds;

    // Resize main delay buffer (power of two)
    const int maxSamples = static_cast<int>(maxDelayTimeSeconds * sampleRate) + 1;
    const int bufferSize = juce::nextPowerOfTwo(maxSamples);

    delayBuffer.setSize(numFdnChannels, bufferSize, false, true, false);
    delayBuffer.clear();
    std::fill(writePos.begin(), writePos.end(), 0);

    // Resize input buffer for pre-delay and dry tap
    const int maxInputSamples = static_cast<int>(maxPreDelayMs * 0.001f * sampleRate) + bufferSize;
    inputBuffer.setSize(1, juce::nextPowerOfTwo(maxInputSamples), false, true, false);
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
    UpdateDiffusionNetwork();
}

void DiffusedDelayReverb::SetWetDryMix(float mix)
{
    wetDryMix = juce::jlimit(0.0f, 1.0f, mix);
    smoothedWetDry.setTargetValue(wetDryMix);
}

//==============================================================================
void DiffusedDelayReverb::ProcessBlock(juce::AudioBuffer<float>& AudioBuffer)
{
    const int NumSamples   = AudioBuffer.getNumSamples();
    const int NumChannels  = AudioBuffer.getNumChannels();

    // ---- Smoothing (per-sample) ----
    smoothedDiffusionAmount.skip(NumSamples);
    smoothedWetDry.skip(NumSamples);

    const float PreDelaySamples = delayTimeSeconds * sampleRate;

    for (int Channel = 0; Channel < NumChannels; ++Channel)
    {
        float* ChannelData = AudioBuffer.getWritePointer(Channel);
        float* InputData   = inputBuffer.getWritePointer(0);

        for (int Sample = 0; Sample < NumSamples; ++Sample)
        {
            const float InputSample = ChannelData[Sample];

            // --- 1. Write to input buffer ---
            InputData[inputWritePos] = InputSample;
            const int NextInputWritePos = (inputWritePos + 1) % inputBuffer.getNumSamples();

            // --- 2. Dry tap ---
            const int DryReadPos = (inputWritePos - static_cast<int>(PreDelaySamples) + inputBuffer.getNumSamples()) % inputBuffer.getNumSamples();
            const float DryDelay = InputData[DryReadPos];

            // --- 4. Blend ---
            const float CurrentDiffusionAmount = smoothedDiffusionAmount.getNextValue();

            float FdnInput;

            if (CurrentDiffusionAmount < 0.001f)
            {
                FdnInput = DryDelay;
                // Optional: set feedbackMatrix to identity or diagonal, so only each channel's own delay is used.
            }
            else
            {
                float Diffused = diffusionStage.Process(DryDelay);
                FdnInput = Diffused;
                // Normal cross-matrix feedback
            }

            const float PerChannelInput = FdnInput / static_cast<float>(numFdnChannels);

            // --- 5. Process ALL FDN channels for this sample ---
            std::array<float, numFdnChannels> FdnOutputs{};
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

                // *** ADVANCE WRITE POS PER SAMPLE ***
                writePos[FdnChannel] = (writePos[FdnChannel] + 1) % BufferSize;

                FdnOutputs[FdnChannel] = DelayedSample;
                FdnOutputSum += DelayedSample;
            }

            // --- 6. Wet/dry mix ---
            const float CurrentWetDry = smoothedWetDry.getNextValue();
            const float WetSignal = FdnOutputSum * CurrentWetDry;
            const float DrySignal = InputSample * (1.0f - CurrentWetDry);

            ChannelData[Sample] = DrySignal + WetSignal;

            // --- 7. Advance input write pos ---
            inputWritePos = NextInputWritePos;
        }
    }
}

//==============================================================================
void DiffusedDelayReverb::UpdateDelayBuffer()
{
    const int bufferSize = delayBuffer.getNumSamples();

    if (diffusionAmount < 0.001f)
    {
        // PURE DELAY: All FDN lines are set to the requested delay time
        int pureDelaySamples = juce::jlimit(10, bufferSize - 1, static_cast<int>(delayTimeSeconds * sampleRate));

        for (int i = 0; i < numFdnChannels; ++i)
        {
            const float delayTimeInSeconds = delaySamples[i] / sampleRate;

            delaySamples[i] = pureDelaySamples;
            feedbackGains[i] = std::pow(0.001f, delayTimeInSeconds / feedbackTimeSeconds);
        }

        // Set feedbackMatrix to identity!
        feedbackMatrix.identity(0.0f);

        for (int i = 0; i < numFdnChannels; ++i)
            feedbackMatrix(i, i) = 1.0f;
    }
    else
    {
        // REVERB: Short prime delays, cross-mixing
        const std::array<int, numFdnChannels> basePrimes = { 29, 37, 41, 53 };
        const float scale = delayTimeSeconds / 0.5f; // Normalize around 500ms

        for (int i = 0; i < numFdnChannels; ++i)
        {
            const int baseMs = basePrimes[i];
            const float targetMs = baseMs * scale;
            const float delayTimeInSeconds = delaySamples[i] / sampleRate;

            delaySamples[i] = juce::jlimit(10, bufferSize - 1, static_cast<int>(targetMs * 0.001f * sampleRate));
            feedbackGains[i] = std::pow(0.001f, delayTimeInSeconds / feedbackTimeSeconds);
        }

        UpdateFeedbackMatrix(); // Hadamard or Householder, as you do now
    }
}

void DiffusedDelayReverb::UpdateFeedbackMatrix()
{
    const float s = 1.0f / std::sqrt(2.0f);
    feedbackMatrix(0,0) =  s; feedbackMatrix(0,1) =  s; feedbackMatrix(0,2) =  s; feedbackMatrix(0,3) =  s;
    feedbackMatrix(1,0) =  s; feedbackMatrix(1,1) =  s; feedbackMatrix(1,2) = -s; feedbackMatrix(1,3) = -s;
    feedbackMatrix(2,0) =  s; feedbackMatrix(2,1) = -s; feedbackMatrix(2,2) =  s; feedbackMatrix(2,3) = -s;
    feedbackMatrix(3,0) =  s; feedbackMatrix(3,1) = -s; feedbackMatrix(3,2) = -s; feedbackMatrix(3,3) =  s;
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

    // ----- 5. Advance write position (done outside in ProcessBlock) -----
    // (no increment here – caller does it in bulk)

    // ----- 6. Return the delayed sample (what the mixer hears) -----
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
    currentSize = Size;
    currentQuality = Quality;

    const int MaxSamples = static_cast<int>(200.0f * SampleRate / 44100.0f);

    // ---- SERIES DIFFUSERS -------------------------------------------------
    numSeries = 1 + static_cast<int>(Quality * 3);  // 1 to 4
    seriesDiffusers.clear();
    seriesDiffusers.shrink_to_fit();
    seriesDiffusers.reserve(numSeries);

    for (int Index = 0; Index < numSeries; ++Index)
    {
        seriesDiffusers.emplace_back();                    // Direct construct
        seriesDiffusers.back().Prepare(SampleRate, MaxSamples);
    }

    // ---- PARALLEL DIFFUSERS -----------------------------------------------
    numParallel = 2 + static_cast<int>(Quality * 4);  // 2 to 6
    parallelDiffusers.clear();
    parallelDiffusers.shrink_to_fit();
    parallelDiffusers.reserve(numParallel);

    for (int Index = 0; Index < numParallel; ++Index)
    {
        parallelDiffusers.emplace_back();
        parallelDiffusers.back().Prepare(SampleRate, MaxSamples);
    }

    UpdateParameters(Size, Quality);
}

void DiffusedDelayReverb::DiffusionStage::UpdateParameters(float size, float quality)
{
    currentSize = size;
    currentQuality = quality;

    const float baseSizeMs = 5.0f + size * 40.0f; // 5 to 45 ms
    const int baseSamples = static_cast<int>(baseSizeMs * 0.001f * 44100.0f);

    // Prime offsets for incommensurate delays
    const std::vector<int> primes = { 2, 3, 5, 7, 11, 13, 17, 19, 23 };

    // Series chain
    for (int i = 0; i < seriesDiffusers.size(); ++i)
    {
        const int offset = primes[i % primes.size()];
        const int samples = baseSamples * (i + 1) * offset / 7;
        seriesDiffusers[i].SetDelaySamples(samples);
    }

    // Parallel bank
    for (int i = 0; i < parallelDiffusers.size(); ++i)
    {
        const int offset = primes[(i + 3) % primes.size()];
        const int samples = baseSamples * offset / 5;
        parallelDiffusers[i].SetDelaySamples(samples);
    }
}

float DiffusedDelayReverb::DiffusionStage::Process(float input)
{
    float out = input;

    // Series diffusion
    for (auto& d : seriesDiffusers)
        out = d.Process(out);

    // Parallel diffusion (sum)
    float parallelSum = 0.0f;

    for (auto& d : parallelDiffusers)
        parallelSum += d.Process(out);

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