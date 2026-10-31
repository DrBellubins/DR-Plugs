#include "MasterFx.h"

#include <algorithm>
#include <cmath>

namespace gmosh
{
namespace
{
// Sylvester Hadamard matrix, symmetric and orthogonal: the FDN mixing
// matrix stays energy preserving for any feedback gain.
const signed char kHadamard[8][8] = {
    { +1, +1, +1, +1, +1, +1, +1, +1 }, { +1, -1, +1, -1, +1, -1, +1, -1 },
    { +1, +1, -1, -1, +1, +1, -1, -1 }, { +1, -1, -1, +1, +1, -1, -1, +1 },
    { +1, +1, +1, +1, -1, -1, -1, -1 }, { +1, -1, +1, -1, -1, +1, -1, +1 },
    { +1, +1, -1, -1, -1, -1, +1, +1 }, { +1, -1, -1, +1, -1, +1, +1, -1 },
};

// Mutually prime-ish base lengths in samples at 48 kHz. Mutually prime
// lengths are what stop the FDN from ringing like a bell.
const int kBaseLength[8] = { 1581, 1867, 2053, 2293, 2531, 2699, 2917, 3121 };

constexpr float kInvSqrt8 = 0.35355339f;

/// The per line feedback gain that empties a line of rt60Seconds in that time.
float rt60ToFeedbackGain(float lineLengthSeconds, float rt60Seconds)
{
    if (rt60Seconds <= 0.01f)
        return 0.0f;

    const float feedbackGain = std::pow(10.0f, -3.0f * lineLengthSeconds / rt60Seconds);

    // A line can never quite reach unity or the network rings for ever.
    return std::min(feedbackGain, 0.9995f);
}
} // namespace

//==============================================================================
void MasterFx::prepare(double sampleRateHz, int maxBlockSamples)
{
    sampleRate = sampleRateHz;
    const float sampleRateRatio = static_cast<float>(sampleRateHz) / 48000.0f;

    for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
    {
        baseLengthSamples[lineIndex] = static_cast<float>(kBaseLength[lineIndex]) * sampleRateRatio;
        const int maxLineLengthSamples =
            static_cast<int>(baseLengthSamples[lineIndex] * 1.10f) + maxBlockSamples + 32;
        delayLines[lineIndex].Prepare(maxLineLengthSamples);
        lengthCurrentSamples[lineIndex] = lengthTargetSamples[lineIndex] =
            baseLengthSamples[lineIndex] * sizeCurrent;
        lineModPhase[lineIndex] = static_cast<float>(lineIndex * 37 % 100) * 0.01f;
        lineModulationIncrement[lineIndex] =
            (0.13f + 0.11f * static_cast<float>(lineIndex % 3)) / static_cast<float>(sampleRateHz);
    }

    for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
    {
        inGainL[lineIndex] = static_cast<float>(kHadamard[lineIndex][0]) * kInvSqrt8;
        inGainR[lineIndex] = static_cast<float>(kHadamard[lineIndex][1]) * kInvSqrt8;
        outGainL[lineIndex] = static_cast<float>(kHadamard[lineIndex][2]) * kInvSqrt8;
        outGainR[lineIndex] = static_cast<float>(kHadamard[lineIndex][3]) * kInvSqrt8;
    }

    chorusDelayL.Prepare(static_cast<int>(sampleRateHz * 0.08f) + maxBlockSamples + 8);
    chorusDelayR.Prepare(static_cast<int>(sampleRateHz * 0.08f) + maxBlockSamples + 8);
    chorusModulatorL.setShape(0, randomGenerator);
    chorusModulatorR.setShape(0, randomGenerator);
    chorusModulatorR.setPhase(0.37f);

    randomGenerator = Rng(0x6E0F5E0Fu);
    reset();
}

//==============================================================================
void MasterFx::reset()
{
    for (auto& delayLine : delayLines)
        delayLine.Reset();

    for (auto& dampFilter : lineDampFilters)
        dampFilter.reset();

    chorusDelayL.Reset();
    chorusDelayR.Reset();
    chorusModulatorL.reset();
    chorusModulatorR.reset();

    for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
    {
        lineInput[lineIndex] = 0.0f;
        lineOutput[lineIndex] = 0.0f;
    }
}

//==============================================================================
void MasterFx::setParams(float newSize, float newRt60Seconds, float newDamping, float newChorus, float newMix,
                         float newWidth)
{
    sizeTarget = clampf(newSize, 0.2f, 1.0f);
    rt60Target = clampf(newRt60Seconds, 0.02f, 16.0f);
    dampTarget = clamp01(newDamping);
    chorusTarget = clamp01(newChorus);
    mixTarget = clamp01(newMix);
    widthTarget = clamp01(newWidth);
}

//==============================================================================
void MasterFx::updateLineLengths()
{
    const float rt60Seconds = rt60Current;

    for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
    {
        lengthTargetSamples[lineIndex] = baseLengthSamples[lineIndex] * sizeCurrent;
        feedback[lineIndex] =
            rt60ToFeedbackGain(lengthTargetSamples[lineIndex] / static_cast<float>(sampleRate), rt60Seconds);

        const float dampingHz = std::exp(lerpf(std::log(18000.0f), std::log(700.0f), dampCurrent));
        lineDampFilters[lineIndex].setHz(std::min(dampingHz, static_cast<float>(sampleRate) * 0.45f),
                                         sampleRate);
    }
}

//==============================================================================
void MasterFx::process(float* left, float* right, int numSamples)
{
    const float ctrlSmoothing = 1.0f - std::exp(-1.0f / (0.08f * static_cast<float>(sampleRate)));

    const float chorusBase = static_cast<float>(sampleRate) * 0.017f;

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex)
    {
        // Parameter smoothing: growing a branch moves size/length/mix, and the
        // tail must not click (design doc 8).
        sizeCurrent += (sizeTarget - sizeCurrent) * ctrlSmoothing;
        rt60Current += (rt60Target - rt60Current) * ctrlSmoothing;
        dampCurrent += (dampTarget - dampCurrent) * ctrlSmoothing;
        chorusCurrent += (chorusTarget - chorusCurrent) * ctrlSmoothing;
        mixCurrent += (mixTarget - mixCurrent) * ctrlSmoothing;
        widthCurrent += (widthTarget - widthCurrent) * ctrlSmoothing;

        if (--ctrlCountdown <= 0)
        {
            ctrlCountdown = kCtrlPeriod;
            updateLineLengths();
        }

        const float inputL = left[sampleIndex];
        const float inputR = right[sampleIndex];

        //--------------------------------------------------------------------
        // Chorus stage: two decorrelated modulated taps, depth and rate scale
        // together with fx_chors.
        const float chorusDepth = chorusCurrent * static_cast<float>(sampleRate) * 0.007f;
        const float chorusRate = 0.18f + chorusCurrent * 0.65f;
        chorusModulatorL.setRateHz(chorusRate, sampleRate);
        chorusModulatorR.setRateHz(chorusRate * 1.137f, sampleRate);

        chorusDelayL.Push(inputL);
        chorusDelayR.Push(inputR);

        const float chorusDelaySamplesL =
            chorusBase + chorusDepth * (0.5f + 0.5f * chorusModulatorL.process());
        const float chorusDelaySamplesR =
            chorusBase + chorusDepth * (0.5f + 0.5f * chorusModulatorR.process());

        const float chorusSignalL = chorusDelayL.Read(chorusDelaySamplesL);
        const float chorusSignalR = chorusDelayR.Read(chorusDelaySamplesR);

        const float chorusFeedL = lerpf(inputL, chorusSignalL, chorusCurrent);
        const float chorusFeedR = lerpf(inputR, chorusSignalR, chorusCurrent);

        //--------------------------------------------------------------------
        // FDN reverb.
        for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
        {
            // Slight delay length modulation kills metallic ringing.
            lineModPhase[lineIndex] += lineModulationIncrement[lineIndex];

            if (lineModPhase[lineIndex] >= 1.0f)
                lineModPhase[lineIndex] -= 1.0f;

            lengthCurrentSamples[lineIndex] +=
                (lengthTargetSamples[lineIndex] - lengthCurrentSamples[lineIndex])
                * (1.0f / static_cast<float>(kCtrlPeriod));

            const float lengthModulation = std::sin(lineModPhase[lineIndex] * 2.0f * kPi) * 0.7f;
            lineInput[lineIndex] = lineDampFilters[lineIndex].process(
                delayLines[lineIndex].Read(lengthCurrentSamples[lineIndex] + lengthModulation));
        }

        for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
        {
            float hadamardSum = 0.0f;

            for (std::size_t mixIndex = 0; mixIndex < kFdnLines; ++mixIndex)
                hadamardSum += static_cast<float>(kHadamard[lineIndex][mixIndex]) * lineInput[mixIndex];

            lineOutput[lineIndex] = hadamardSum * kInvSqrt8 * feedback[lineIndex];
        }

        for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
            delayLines[lineIndex].Push(lineOutput[lineIndex] + chorusFeedL * inGainL[lineIndex]
                                       + chorusFeedR * inGainR[lineIndex]);

        float wetL = 0.0f;
        float wetR = 0.0f;

        for (std::size_t lineIndex = 0; lineIndex < kFdnLines; ++lineIndex)
        {
            wetL += lineOutput[lineIndex] * outGainL[lineIndex];
            wetR += lineOutput[lineIndex] * outGainR[lineIndex];
        }

        // Width control on the wet side only, so the dry stays centred.
        const float widthScale = lerpf(0.25f, 1.6f, widthCurrent);
        const float midComponent = (wetL + wetR) * 0.5f;
        const float sideComponent = (wetL - wetR) * 0.5f * widthScale;
        wetL = midComponent + sideComponent;
        wetR = midComponent - sideComponent;

        // Equal power wet/dry.
        const float angle = mixCurrent * 0.5f * kPi;
        const float dryGain = std::cos(angle);
        // The FDN taps are read conservatively, so the wet needs the make up
        // gain to match the dry path.
        const float wetGain = std::sin(angle) * 2.2f;

        left[sampleIndex] = inputL * dryGain + wetL * wetGain;
        right[sampleIndex] = inputR * dryGain + wetR * wetGain;
    }
}

} // namespace gmosh
