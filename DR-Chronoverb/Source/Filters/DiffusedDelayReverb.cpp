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
    smoothedFeedbackAmount.reset(44100, 0.05f);
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
    smoothedFeedbackAmount.setCurrentAndTargetValue(feedbackAmount);
}

//==============================================================================
void DiffusedDelayReverb::SetDelayTime(float timeSeconds)
{
    delayTimeSeconds = juce::jlimit(0.001f, maxDelayTimeSeconds, timeSeconds);
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

void DiffusedDelayReverb::SetFeedback(float FeedbackAmount)
{
    feedbackAmount = juce::jlimit(0.0f, 1.0f, FeedbackAmount);
    smoothedFeedbackAmount.setTargetValue(feedbackAmount);
}

void DiffusedDelayReverb::SetWetDryMix(float mix)
{
    wetDryMix = juce::jlimit(0.0f, 1.0f, mix);
    smoothedWetDry.setTargetValue(wetDryMix);
}

//==============================================================================
void DiffusedDelayReverb::ProcessBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int bufferSize = delayBuffer.getNumSamples();

    // Update smoothed parameters
    smoothedDiffusionAmount.skip(numSamples);
    smoothedWetDry.skip(numSamples);
    smoothedFeedbackAmount.skip(numSamples);
    
    const float currentDiffAmt = smoothedDiffusionAmount.getNextValue();
    const float currentWetDry = smoothedWetDry.getNextValue();

    // Pre-delay length in samples
    const int preDelaySamples = static_cast<int>(delayTimeSeconds * sampleRate);

    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* channelData = buffer.getWritePointer(channel);
        float* inputData = inputBuffer.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = channelData[i];

            // Write input to circular input buffer
            inputData[inputWritePos] = input;

            // Read dry delayed signal (before diffusion)
            const int dryReadPos = (inputWritePos - preDelaySamples + inputBuffer.getNumSamples()) % inputBuffer.getNumSamples();
            const float dryDelay = inputData[dryReadPos];

            // Apply diffusion stage
            const float diffused = diffusionStage.Process(dryDelay);

            // Blend dry delay and diffused signal
            const float fdnInput = dryDelay * (1.0f - currentDiffAmt) + diffused * currentDiffAmt;

            // Distribute to all FDN channels
            float fdnOutput = 0.0f;

            for (int fdnCh = 0; fdnCh < numFdnChannels; ++fdnCh)
            {
                const float channelInput = fdnInput * 0.25f; // Equal distribution
                fdnOutput += ProcessFDNChannel(fdnCh, channelInput);
            }

            // Final wet/dry mix
            const float wetSignal = fdnOutput * currentWetDry;
            const float drySignal = input * (1.0f - currentWetDry);

            // ADD THIS LINE to print first sample of first channel (or wherever you need)
            if (channel == 0 && i < 3) // Just log first 3 samples for brevity
            {
                juce::Logger::outputDebugString("wet: " + juce::String(wetSignal));
            }

            channelData[i] = drySignal + wetSignal;

            // Advance write position
            inputWritePos = (inputWritePos + 1) % inputBuffer.getNumSamples();
        }
    }

    // Advance FDN write positions
    for (auto& pos : writePos)
        pos = (pos + numSamples) % bufferSize;
}

//==============================================================================
void DiffusedDelayReverb::UpdateDelayBuffer()
{
    const int bufferSize = delayBuffer.getNumSamples();

    // Scale prime delays by delay time
    const std::array<int, numFdnChannels> basePrimes = { 29, 37, 41, 53 };
    const float scale = delayTimeSeconds / 0.5f; // Normalize around 500ms

    for (int i = 0; i < numFdnChannels; ++i)
    {
        const int baseMs = basePrimes[i];
        const float targetMs = baseMs * scale;
        delaySamples[i] = juce::jlimit(10, bufferSize - 1, static_cast<int>(targetMs * 0.001f * sampleRate));

        // Compute per-sample feedback decay
        const float decayTime = delayTimeSeconds * 3.0f; // 60 dB decay
        feedbackGains[i] = std::pow(0.001f, delaySamples[i] / (decayTime * sampleRate));
    }

    UpdateFeedbackMatrix();
}

void DiffusedDelayReverb::UpdateFeedbackMatrix()
{
    // Simple orthogonal matrix for diffuse feedback
    for (int i = 0; i < numFdnChannels; ++i)
    {
        for (int j = 0; j < numFdnChannels; ++j)
        {
            feedbackMatrix(i, j) = (i == j ? 0.5f : -0.25f);
        }
    }
}

float DiffusedDelayReverb::ProcessFDNChannel(int channel, float input)
{
    float* delayData = delayBuffer.getWritePointer(channel);
    const int bufferSize = delayBuffer.getNumSamples();
    const int readPos = (writePos[channel] - delaySamples[channel] + bufferSize) % bufferSize;

    const float delayed = delayData[readPos];

    // --- Add this: fetch latest smoothed feedback value
    const float feedback = smoothedFeedbackAmount.getNextValue();

    // Apply feedback from all channels
    float feedbackSum = 0.0f;
    for (int src = 0; src < numFdnChannels; ++src)
    {
        float* srcData = delayBuffer.getWritePointer(src);
        const int srcReadPos = (writePos[src] - delaySamples[src] + bufferSize) % bufferSize;
        feedbackSum += feedbackMatrix(channel, src) * srcData[srcReadPos];
    }

    // --- Apply feedback scaling
    const float output = delayed + (feedbackSum * feedback * feedbackGains[channel]);

    // If you want to optionally preserve "decay time" control, combine as you see fit:
    // float output = delayed + (feedbackSum * feedback * feedbackGains[channel]);

    delayData[writePos[channel]] = input + output * 0.7f;

    return output;
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