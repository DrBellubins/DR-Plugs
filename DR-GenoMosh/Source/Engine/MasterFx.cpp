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
        { +1, +1, +1, +1, +1, +1, +1, +1 },
        { +1, -1, +1, -1, +1, -1, +1, -1 },
        { +1, +1, -1, -1, +1, +1, -1, -1 },
        { +1, -1, -1, +1, +1, -1, -1, +1 },
        { +1, +1, +1, +1, -1, -1, -1, -1 },
        { +1, -1, +1, -1, -1, +1, -1, +1 },
        { +1, +1, -1, -1, -1, -1, +1, +1 },
        { +1, -1, -1, +1, -1, +1, +1, -1 },
    };

    // Mutually prime-ish base lengths in samples at 48 kHz. Mutually prime
    // lengths are what stop the FDN from ringing like a bell.
    const int kBaseLength[8] = { 1581, 1867, 2053, 2293, 2531, 2699, 2917, 3121 };

    constexpr float kInvSqrt8 = 0.35355339f;

    float rt60ToFeedback (float lengthSeconds, float rt60)
    {
        if (rt60 <= 0.01f)
            return 0.0f;
        const float g = std::pow (10.0f, -3.0f * lengthSeconds / rt60);
        return std::min (g, 0.9995f);
    }
}

//==============================================================================
void MasterFx::prepare (double sampleRateHz, int maxBlockSamples)
{
    sampleRate = sampleRateHz;
    const float srRatio = static_cast<float> (sampleRateHz) / 48000.0f;

    for (std::size_t k = 0; k < kFdnLines; ++k)
    {
        baseLength[k] = static_cast<float> (kBaseLength[k]) * srRatio;
        const int maxLen = static_cast<int> (baseLength[k] * 1.10f) + maxBlockSamples + 32;
        lines[k].Prepare (maxLen);
        lengthCur[k] = lengthTo[k] = baseLength[k] * sizeCurrent;
        lineModPhase[k] = static_cast<float> (k * 37 % 100) * 0.01f;
        lineModInc[k] = (0.13f + 0.11f * static_cast<float> (k % 3)) / static_cast<float> (sampleRateHz);
    }

    for (std::size_t k = 0; k < kFdnLines; ++k)
    {
        inGainL[k]  = static_cast<float> (kHadamard[k][0]) * kInvSqrt8;
        inGainR[k]  = static_cast<float> (kHadamard[k][1]) * kInvSqrt8;
        outGainL[k] = static_cast<float> (kHadamard[k][2]) * kInvSqrt8;
        outGainR[k] = static_cast<float> (kHadamard[k][3]) * kInvSqrt8;
    }

    chorusL.Prepare (static_cast<int> (sampleRateHz * 0.08f) + maxBlockSamples + 8);
    chorusR.Prepare (static_cast<int> (sampleRateHz * 0.08f) + maxBlockSamples + 8);
    chorusModL.setShape (0, rng);
    chorusModR.setShape (0, rng);
    chorusModR.setPhase (0.37f);

    rng = Rng (0x6E0F5E0Fu);
    reset();
}

//==============================================================================
void MasterFx::reset()
{
    for (auto& l : lines) l.Reset();
    for (auto& d : lineDamp) d.reset();
    chorusL.Reset();
    chorusR.Reset();
    chorusModL.reset();
    chorusModR.reset();
    for (std::size_t k = 0; k < kFdnLines; ++k) { xIn[k] = 0.0f; yOut[k] = 0.0f; }
}

//==============================================================================
void MasterFx::setParams (float size, float rt60, float damp, float chorus, float mix, float width)
{
    sizeTarget = clampf (size, 0.2f, 1.0f);
    rt60Target = clampf (rt60, 0.02f, 16.0f);
    dampTarget = clamp01 (damp);
    chorusTarget = clamp01 (chorus);
    mixTarget = clamp01 (mix);
    widthTarget = clamp01 (width);
}

//==============================================================================
void MasterFx::updateLineLengths()
{
    const float rt = rt60Current;

    for (std::size_t k = 0; k < kFdnLines; ++k)
    {
        lengthTo[k] = baseLength[k] * sizeCurrent;
        feedback[k] = rt60ToFeedback (lengthTo[k] / static_cast<float> (sampleRate), rt);

        const float hz = std::exp (lerpf (std::log (18000.0f), std::log (700.0f), dampCurrent));
        lineDamp[k].setHz (std::min (hz, static_cast<float> (sampleRate) * 0.45f), sampleRate);
    }
}

//==============================================================================
void MasterFx::process (float* left, float* right, int numSamples)
{
    const float ctrlSmoothing = 1.0f - std::exp (-1.0f / (0.08f * static_cast<float> (sampleRate)));

    const float chorusBase = static_cast<float> (sampleRate) * 0.017f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Parameter smoothing: growing a branch moves size/length/mix, and the
        // tail must not click (design doc 8).
        sizeCurrent   += (sizeTarget - sizeCurrent) * ctrlSmoothing;
        rt60Current   += (rt60Target - rt60Current) * ctrlSmoothing;
        dampCurrent   += (dampTarget - dampCurrent) * ctrlSmoothing;
        chorusCurrent += (chorusTarget - chorusCurrent) * ctrlSmoothing;
        mixCurrent    += (mixTarget - mixCurrent) * ctrlSmoothing;
        widthCurrent  += (widthTarget - widthCurrent) * ctrlSmoothing;

        if (--ctrlCountdown <= 0)
        {
            ctrlCountdown = kCtrlPeriod;
            updateLineLengths();
        }

        const float dryL = left[i];
        const float dryR = right[i];

        //--------------------------------------------------------------------
        // Chorus stage: two decorrelated modulated taps, depth and rate scale
        // together with fx_chors.
        const float chorusDepth = chorusCurrent * static_cast<float> (sampleRate) * 0.007f;
        const float chorusRate = 0.18f + chorusCurrent * 0.65f;
        chorusModL.setRateHz (chorusRate, sampleRate);
        chorusModR.setRateHz (chorusRate * 1.137f, sampleRate);

        chorusL.Push (dryL);
        chorusR.Push (dryR);

        const float dL = chorusBase + chorusDepth * (0.5f + 0.5f * chorusModL.process());
        const float dR = chorusBase + chorusDepth * (0.5f + 0.5f * chorusModR.process());

        const float chL = chorusL.Read (dL);
        const float chR = chorusR.Read (dR);

        const float feedL = lerpf (dryL, chL, chorusCurrent);
        const float feedR = lerpf (dryR, chR, chorusCurrent);

        //--------------------------------------------------------------------
        // FDN reverb.
        for (std::size_t k = 0; k < kFdnLines; ++k)
        {
            // Slight delay length modulation kills metallic ringing.
            lineModPhase[k] += lineModInc[k];
            if (lineModPhase[k] >= 1.0f) lineModPhase[k] -= 1.0f;
            lengthCur[k] += (lengthTo[k] - lengthCur[k]) * (1.0f / static_cast<float> (kCtrlPeriod));

            const float mod = std::sin (lineModPhase[k] * 2.0f * kPi) * 0.7f;
            xIn[k] = lineDamp[k].process (lines[k].Read (lengthCur[k] + mod));
        }

        for (std::size_t k = 0; k < kFdnLines; ++k)
        {
            float sum = 0.0f;
            for (std::size_t j = 0; j < kFdnLines; ++j)
                sum += static_cast<float> (kHadamard[k][j]) * xIn[j];

            yOut[k] = sum * kInvSqrt8 * feedback[k];
        }

        for (std::size_t k = 0; k < kFdnLines; ++k)
            lines[k].Push (yOut[k] + feedL * inGainL[k] + feedR * inGainR[k]);

        float wetL = 0.0f, wetR = 0.0f;
        for (std::size_t k = 0; k < kFdnLines; ++k)
        {
            wetL += yOut[k] * outGainL[k];
            wetR += yOut[k] * outGainR[k];
        }

        // Width control on the wet side only, so the dry stays centred.
        const float widthScale = lerpf (0.25f, 1.6f, widthCurrent);
        const float mid = (wetL + wetR) * 0.5f;
        const float side = (wetL - wetR) * 0.5f * widthScale;
        wetL = mid + side;
        wetR = mid - side;

        // Equal power wet/dry.
        const float angle = mixCurrent * 0.5f * kPi;
        const float dryGain = std::cos (angle);
        const float wetGain = std::sin (angle) * 2.2f; // FDN taps are conservative

        left[i]  = dryL * dryGain + wetL * wetGain;
        right[i] = dryR * dryGain + wetR * wetGain;
    }
}

} // namespace gmosh
