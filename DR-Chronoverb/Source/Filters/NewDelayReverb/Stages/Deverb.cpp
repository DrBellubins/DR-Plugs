#include "Deverb.h"

void Deverb::PrepareToPlay(double newSampleRate)
{
    sampleRate = std::max(1.0, newSampleRate);

    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();

    delayLineLeft = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);
    delayLineRight = std::make_unique<DelayLine>(delayTimeSegment.MaxDelaySamples);

    delayLineLeft->SetSampleRate(sampleRate);
    delayLineRight->SetSampleRate(sampleRate);

    delayLineLeft->Clear();
    delayLineRight->Clear();

    dampingLeft = std::make_unique<DampingFilter>();
    dampingRight = std::make_unique<DampingFilter>();

    dampingLeft->Prepare(sampleRate);
    dampingRight->Prepare(sampleRate);

    dampingLeft->SetCutoffHz(7000.0f);
    dampingRight->SetCutoffHz(7000.0f);

    diffusionLeft.Prepare(sampleRate);
    diffusionRight.Prepare(sampleRate);

    diffusionLeft.SetQuality(diffusionQualityStages);
    diffusionRight.SetQuality(diffusionQualityStages);

    diffusionLeft.SetSize(diffusionSize);
    diffusionRight.SetSize(diffusionSize);

    diffusionLeft.SetDiffusionAmount(diffusionAmount);
    diffusionRight.SetDiffusionAmount(diffusionAmount);

    updateFeedbackGainFromFeedbackTime();

    smoothedBlend = diffusionLeft.GetBlendAmount();
    smoothedReadDelayMs = delayTimeSegment.DelayTimeMilliseconds;

    blendSlewCoefficient = 1.0f / (0.01f * static_cast<float>(sampleRate)); // ~10 ms
    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;

    Reset();
}

void Deverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    juce::ignoreUnused(audioBuffer);

    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;
}

std::pair<float, float> Deverb::ProcessSample(float inputSampleL, float inputSampleR)
{
    // 1) Input + feedback
    const float inputWithFeedbackL = inputSampleL + lastFeedbackL;
    const float inputWithFeedbackR = inputSampleR + lastFeedbackR;

    // 2) Diffused path
    const float diffusedL = diffusionLeft.ProcessSample(inputWithFeedbackL);
    const float diffusedR = diffusionRight.ProcessSample(inputWithFeedbackR);

    // 3) Smooth only the clean/diffused write blend
    const float targetBlend = diffusionLeft.GetBlendAmount();
    smoothedBlend += blendSlewCoefficient * (targetBlend - smoothedBlend);
    smoothedBlend = std::clamp(smoothedBlend, 0.0f, 1.0f);

    // 4) Blend clean path and diffused path BEFORE delay write
    const float writeSignalL =
        (inputWithFeedbackL * (1.0f - smoothedBlend)) + (diffusedL * smoothedBlend);

    const float writeSignalR =
        (inputWithFeedbackR * (1.0f - smoothedBlend)) + (diffusedR * smoothedBlend);

    // 5) Write to delay line
    delayLineLeft->PushSample(writeSignalL);
    delayLineRight->PushSample(writeSignalR);

    // 6) Read only at the user delay time
    // This means diffusionAmount no longer changes timing -> no pitch slewing from amount.
    const float targetReadDelayMs =
        std::max(1.0f, delayTimeSegment.DelayTimeMilliseconds);

    smoothedReadDelayMs += readDelaySlewCoefficient
        * (targetReadDelayMs - smoothedReadDelayMs);

    smoothedReadDelayMs = std::max(1.0f, smoothedReadDelayMs);

    const float wetL = delayLineLeft->ReadFeedbackBuffer(smoothedReadDelayMs);
    const float wetR = delayLineRight->ReadFeedbackBuffer(smoothedReadDelayMs);

    // 7) Damping
    const float dampedL = dampingLeft->ProcessSample(wetL);
    const float dampedR = dampingRight->ProcessSample(wetR);

    // 8) Recirculation
    lastFeedbackL = dampedL * feedbackGain;
    lastFeedbackR = dampedR * feedbackGain;

    return { dampedL, dampedR };
}

void Deverb::Reset()
{
    lastFeedbackL = 0.0f;
    lastFeedbackR = 0.0f;

    smoothedBlend = diffusionLeft.GetBlendAmount();
    smoothedReadDelayMs = std::max(1.0f, delayTimeSegment.DelayTimeMilliseconds);

    if (delayLineLeft != nullptr)
        delayLineLeft->Clear();

    if (delayLineRight != nullptr)
        delayLineRight->Clear();

    if (dampingLeft != nullptr)
        dampingLeft->Reset();

    if (dampingRight != nullptr)
        dampingRight->Reset();

    diffusionLeft.Reset();
    diffusionRight.Reset();
}

void Deverb::SetHostTempo(float bpm)
{
    hostBPM = bpm;
    delayTimeSegment.SetHostTempo(hostBPM);
}

void Deverb::SetDelayTime(float newDelayTime)
{
    delayTimeSegment.SetDelayTime(newDelayTime);
}

void Deverb::SetDelayMode(int newDelayMode)
{
    delayTimeSegment.SetDelayMode(newDelayMode);
}

void Deverb::SetFeedbackTime(float newFeedbackTimeSeconds)
{
    feedbackTimeSeconds = std::max(0.0f, newFeedbackTimeSeconds);
    updateFeedbackGainFromFeedbackTime();
}

void Deverb::SetDiffusionAmount(float newAmount01)
{
    diffusionAmount = std::clamp(newAmount01, 0.0f, 1.0f);

    diffusionLeft.SetDiffusionAmount(diffusionAmount);
    diffusionRight.SetDiffusionAmount(diffusionAmount);
}

void Deverb::SetDiffusionSize(float newSize01)
{
    diffusionSize = std::clamp(newSize01, 0.0f, 1.0f);

    diffusionLeft.SetSize(diffusionSize);
    diffusionRight.SetSize(diffusionSize);
}

void Deverb::SetDiffusionQuality(int newQualityStages)
{
    diffusionQualityStages = std::clamp(newQualityStages, 1, DeverbDiffusionChain::MaxStages);

    diffusionLeft.SetQuality(diffusionQualityStages);
    diffusionRight.SetQuality(diffusionQualityStages);
}

void Deverb::updateFeedbackGainFromFeedbackTime()
{
    const float normalized = std::clamp(feedbackTimeSeconds / 10.0f, 0.0f, 1.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}