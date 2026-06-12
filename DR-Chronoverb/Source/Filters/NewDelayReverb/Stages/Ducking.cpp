#include "Ducking.h"

void Ducking::PrepareToPlay(double newSampleRate)
{
    sampleRate = std::max(1.0, newSampleRate);
    UpdateTimeCoefficients();
    Reset();
}

void Ducking::Reset()
{
    detectorEnvelope = 0.0f;
    appliedGain = 1.0f;
}

std::pair<float, float> Ducking::ProcessSample(float dryL, float dryR, float wetL, float wetR)
{
    const float dryEnvelope = ComputeEnvelopeFromDry(dryL, dryR);

    // Fast attack when detector rises, slower release when it falls.
    if (dryEnvelope > detectorEnvelope)
        detectorEnvelope += (dryEnvelope - detectorEnvelope) * attackCoefficient;
    else
        detectorEnvelope += (dryEnvelope - detectorEnvelope) * releaseCoefficient;

    detectorEnvelope = std::clamp(detectorEnvelope, 0.0f, 1.0f);

    // Duck depth:
    // amount = 0.0 -> no ducking
    // amount = 1.0 -> up to full attenuation at full detector level
    const float duckControl = std::clamp(duckAmount * detectorEnvelope, 0.0f, 1.0f);
    const float targetGain = std::pow(1.0f - duckControl, 2.0f);

    // Smooth the applied gain too, using same directional timing.
    if (targetGain < appliedGain)
        appliedGain += (targetGain - appliedGain) * attackCoefficient;
    else
        appliedGain += (targetGain - appliedGain) * releaseCoefficient;

    appliedGain = std::clamp(appliedGain, 0.0f, 1.0f);

    return { wetL * appliedGain, wetR * appliedGain };
}

void Ducking::SetDuckAmount(float newAmount01)
{
    duckAmount = std::clamp(newAmount01, 0.0f, 1.0f);
}

void Ducking::SetDuckAttack(float newAttackMs)
{
    attackMs = std::clamp(newAttackMs, 0.0f, 1000.0f);

    DBG("Attack: " << attackMs << " ms");

    UpdateTimeCoefficients();
}

void Ducking::SetDuckRelease(float newReleaseMs)
{
    releaseMs = std::clamp(newReleaseMs, 0.0f, 1000.0f);

    DBG("Release: " << releaseMs << " ms");

    UpdateTimeCoefficients();
}

static int debugCounter = 0;
float Ducking::ComputeEnvelopeFromDry(float dryL, float dryR) const
{
    const float absL = std::abs(dryL);
    const float absR = std::abs(dryR);
    const float peak = std::max(absL, absR);


    if (++debugCounter >= 2048)
    {
        debugCounter = 0;
        DBG("duck env=" << detectorEnvelope << " gain=" << appliedGain);
    }

    // Boost and curve so ordinary signals create meaningful ducking.
    const float boosted = std::clamp(peak * 4.0f, 0.0f, 1.0f);
    return std::pow(boosted, 0.6f);
}

void Ducking::UpdateTimeCoefficients()
{
    const auto msToCoeff = [this](float ms) -> float
    {
        if (ms <= 0.0f)
            return 1.0f; // instant

        const float timeSeconds = ms * 0.001f;
        return 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * timeSeconds));
    };

    attackCoefficient = msToCoeff(attackMs);
    releaseCoefficient = msToCoeff(releaseMs);
}