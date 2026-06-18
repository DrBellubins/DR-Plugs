#include "Deverb.h"

void Deverb::PrepareToPlay(double newSampleRate, Filters& filters)
{
    sampleRate = newSampleRate;
    filtersInput = &filters;

    delayTimeSegment.PrepareToPlay(sampleRate);
    delayTimeSegment.UpdateDelayMilliseconds();

    delayLineLeft = DelayLine(delayTimeSegment.MaxDelaySamples);
    delayLineRight = DelayLine(delayTimeSegment.MaxDelaySamples);

    delayLineLeft.SetSampleRate(sampleRate);
    delayLineRight.SetSampleRate(sampleRate);

    delayLineLeft.Clear();
    delayLineRight.Clear();

    diffusionLeft.Prepare(sampleRate);
    diffusionRight.Prepare(sampleRate);

    diffusionLeft.SetQuality(diffusionQualityStages);
    diffusionRight.SetQuality(diffusionQualityStages);

    diffusionLeft.SetSize(diffusionSize);
    diffusionRight.SetSize(diffusionSize);

    diffusionLeft.SetDiffusionAmount(diffusionAmount);
    diffusionRight.SetDiffusionAmount(diffusionAmount);

    dampingLeft = DampingFilter();
    dampingRight = DampingFilter();

    dampingLeft.Prepare(sampleRate);
    dampingRight.Prepare(sampleRate);

    dampingLeft.SetCutoffHz(dampingCutoff);
    dampingRight.SetCutoffHz(dampingCutoff);

    updateFeedbackGainFromFeedbackTime();

    smoothedBlend = getBlendAmount();
    smoothedReadDelayMs = delayTimeSegment.DelayTimeMilliseconds;

    blendSlewCoefficient = 1.0f / (0.01f * static_cast<float>(sampleRate)); // ~10 ms
    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;

    staticCompensationMs = diffusionLeft.GetTotalChainDelayMs() * diffusionCompensationBias;

    Reset();
}

void Deverb::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    juce::ignoreUnused(audioBuffer);

    readDelaySlewCoefficient = delayTimeSegment.ReadDelaySlewCoefficient;
    updateDynamicDiffusionSizeFromDelayTime();
}

std::pair<float, float> Deverb::ProcessSample(float inputSampleL, float inputSampleR)
{
    // 0) Variables
    const float diffusionAmountLower = getBlendAmount(); // 0.0 - 0.5

    // 1) Input + feedback
    float inputFeedbackL = inputSampleL + lastFeedbackL;
    float inputFeedbackR = inputSampleR + lastFeedbackR;

    // 2) Pre-filters (optional)
    float filteredL = inputFeedbackL;
    float filteredR = inputFeedbackR;

    float filteredFeedbackL = lastFeedbackL;
    float filteredFeedbackR = lastFeedbackR;

    if (filtersOrder == 1)
    {
        auto [outFilteredL, outFilteredR] =
            filtersInput->ProcessSample(lastFeedbackL, lastFeedbackR);

        filteredL = inputSampleL + outFilteredL;
        filteredR = inputSampleR + outFilteredR;

        filteredFeedbackL = outFilteredL;
        filteredFeedbackR = outFilteredR;
    }

    // 2) Clean path
    float cleanTapL = filteredL;
    float cleanTapR = filteredR;

    if (diffusionAmount < 0.5)
    {
        delayLineLeft.PushSample(filteredL);
        delayLineRight.PushSample(filteredR);

        cleanTapL = delayLineLeft.ReadFeedbackBuffer(delayTimeSegment.DelayTimeMilliseconds);
        cleanTapR = delayLineRight.ReadFeedbackBuffer(delayTimeSegment.DelayTimeMilliseconds);
    }

    // 3) Diffused path
    float diffusedTapL = cleanTapL;
    float diffusedTapR = cleanTapR;

    if (diffusionAmount > 0.0001f)
    {
        float swellGain = 1.0f + (1.0f - diffusionAmountLower); // diff amt 0.0 -> 1.0 = 2.0 -> 1.0

        float diffusionInputL = (inputSampleL * swellGain) + filteredFeedbackL;
        float diffusionInputR = (inputSampleR * swellGain) + filteredFeedbackR;

        diffusedTapL = diffusionLeft.ProcessSample(diffusionInputL);
        diffusedTapR = diffusionRight.ProcessSample(diffusionInputR);
    }

    // 4) Blend between clean path and diffused path
    smoothedBlend += blendSlewCoefficient * (diffusionAmountLower - smoothedBlend);
    smoothedBlend = std::clamp(smoothedBlend, 0.0f, 1.0f);

    const float writeSignalL =
        (cleanTapL * (1.0f - smoothedBlend)) + (diffusedTapL * smoothedBlend);

    const float writeSignalR =
        (cleanTapR * (1.0f - smoothedBlend)) + (diffusedTapR * smoothedBlend);

    // 5) Damping
    const float dampedL = dampingLeft.ProcessSample(writeSignalL);
    const float dampedR = dampingRight.ProcessSample(writeSignalR);

    // 6) Recirculation
    lastFeedbackL = dampedL * feedbackGain;
    lastFeedbackR = dampedR * feedbackGain;

    return { dampedL, dampedR };
}

void Deverb::Reset()
{
    lastFeedbackL = 0.0f;
    lastFeedbackR = 0.0f;

    smoothedBlend = getBlendAmount();
    smoothedReadDelayMs = std::max(1.0f, delayTimeSegment.DelayTimeMilliseconds);

    delayLineLeft.Clear();
    delayLineRight.Clear();

    dampingLeft.Reset();
    dampingRight.Reset();

    diffusionLeft.Reset();
    diffusionRight.Reset();
}

//region Utilities
std::pair<DelayLine&, DelayLine&> Deverb::GetDelayLines()
{
    return { delayLineLeft, delayLineRight };
}

float Deverb::getBlendAmount() const
{
    const float a = std::clamp(diffusionAmount, 0.0f, 1.0f);

    // Map 0..0.5 -> 0..1, then clamp so >=0.5 stays fully diffused.
    const float x = std::clamp(a * 2.0f, 0.0f, 1.0f);

    // Equal-power-ish curve into full diffusion.
    return std::sin(x * juce::MathConstants<float>::halfPi);
}

//endregion

//region Parameters

void Deverb::SetHostTempo(float bpm)
{
    hostBPM = bpm;

    delayTimeSegment.SetHostTempo(hostBPM);
    updateDynamicDiffusionSizeFromDelayTime();
}

void Deverb::SetDelayTime(float newDelayTime)
{
    delayTimeSegment.SetDelayTime(newDelayTime);
    updateDynamicDiffusionSizeFromDelayTime();
}

void Deverb::SetDelayMode(int newDelayMode)
{
    delayTimeSegment.SetDelayMode(newDelayMode);
    updateDynamicDiffusionSizeFromDelayTime();
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
    updateDynamicDiffusionSizeFromDelayTime();
}

void Deverb::SetDiffusionQuality(int newQualityStages)
{
    diffusionQualityStages = std::clamp(newQualityStages, 1, DeverbDiffusionChain::MaxStages);

    diffusionLeft.SetQuality(diffusionQualityStages);
    diffusionRight.SetQuality(diffusionQualityStages);
}

void Deverb::SetFiltersOrder(int newOrder)
{
    filtersOrder = newOrder;
}

//endregion

//region Update functions

void Deverb::updateFeedbackGainFromFeedbackTime()
{
    const float normalized = std::clamp(feedbackTimeSeconds / 10.0f, 0.0f, 1.0f);
    const float curved = std::sqrt(normalized);
    feedbackGain = std::max(0.0f, std::min(0.85f * curved, 0.95f));
}

void Deverb::updateDynamicDiffusionSizeFromDelayTime()
{
    const float staticTotalMs = diffusionLeft.GetTotalTuningMs();

    if (staticTotalMs <= 0.0f)
        return;

    // ratio to make diffusion chain nominally match requested delay time
    const float targetRatio =
        delayTimeSegment.DelayTimeMilliseconds / staticTotalMs;

    // Per your request: dynamic length factor * user diffusion size
    const float effectiveSize = targetRatio * diffusionSize;

    //DBG("Delay time: " << delayTimeSegment.DelayTimeMilliseconds << ", totalMs: " << staticTotalMs <<
    //    ", effectiveSize: " << effectiveSize << ", targetRatio: " << targetRatio);

    diffusionLeft.SetSize(effectiveSize);
    diffusionRight.SetSize(effectiveSize);
}

//endregion