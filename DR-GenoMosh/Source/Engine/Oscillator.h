#pragma once

#include "Dsp.h"

#include <cmath>

namespace gmosh
{
//==============================================================================
// Band limited oscillators.
//
// Design doc 7: aliasing is the main reason digital synths sound cheap, so the
// oscillators use PolyBLEP correction. The *_form gene morphs through four
// waveform families with an equal power crossfade; every component is
// individually band limited, so the morph itself cannot zipper.
//==============================================================================

/// PolyBLEP correction (Välimäki/Stiles). phaseSinceTransition is the phase
/// since the last discontinuity in [0,1), phaseIncrement is the phase
/// increment of the oscillator being corrected.
inline float polyBlep(float phaseSinceTransition, float phaseIncrement)
{
    if (phaseIncrement <= 0.0f)
        return 0.0f;

    if (phaseSinceTransition < phaseIncrement)
    {
        const float nearStart = phaseSinceTransition / phaseIncrement;
        return nearStart + nearStart - nearStart * nearStart - 1.0f;
    }

    if (phaseSinceTransition > 1.0f - phaseIncrement)
    {
        const float nearEnd = (phaseSinceTransition - 1.0f) / phaseIncrement;
        return nearEnd * nearEnd + nearEnd + nearEnd + 1.0f;
    }

    return 0.0f;
}

/// Wrap into [0,1).
inline float frac01(float value)
{
    const float wrapped = value - std::floor(value);
    return wrapped < 0.0f ? wrapped + 1.0f : wrapped;
}

class MorphOsc
{
  public:
    void setFrequency(float hertz, float sampleRate)
    {
        increment = hertz / sampleRate;

        if (increment < 0.0f)
            increment = 0.0f;

        if (increment > 0.45f)
            increment = 0.45f;
    }

    void setPhase(float newPhase) { phase = frac01(newPhase); }
    float phaseValue() const { return phase; }
    float incrementValue() const { return increment; }
    void reset() { phase = 0.0f; }

    /// Advance the phase, then render. form: 0 sine, 1/3 triangle, 2/3 saw,
    /// 1 pulse. colour: pulse width / spectral tilt.
    float next(float form, float colour)
    {
        phase += increment;

        if (phase >= 1.0f)
            phase -= 1.0f;

        return render(phase, form, colour);
    }

    /// Render at a phase modulated copy of the current phase (FM/PM).
    float nextModulated(float form, float colour, float phaseModulation)
    {
        phase += increment;

        if (phase >= 1.0f)
            phase -= 1.0f;

        return render(frac01(phase + phaseModulation), form, colour, phase);
    }

    /// Sample and hold output: the value the oscillator had at the start of the
    /// current period (used by the "B shape" structural mutation).
    float heldValue(float form, float colour)
    {
        if (phase < increment)
            heldSample = render(phase, form, colour);

        return heldSample;
    }

    /// correctionPhase is the un modulated phase the PolyBLEP correction is
    /// taken from, which is not the phase being rendered when there is FM.
    float render(float phaseValue, float form, float colour, float correctionPhase = -1.0f) const
    {
        const float correctionPhaseValue = correctionPhase >= 0.0f ? correctionPhase : phaseValue;

        float mixWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const float segment = clampf(form, 0.0f, 1.0f) * 3.0f;
        int segmentIndex = static_cast<int>(segment);

        if (segmentIndex > 2)
            segmentIndex = 2;

        const float segmentFraction = segment - static_cast<float>(segmentIndex);
        mixWeights[segmentIndex] = std::sqrt(1.0f - segmentFraction);
        mixWeights[segmentIndex + 1] = std::sqrt(segmentFraction);

        float outputSample = 0.0f;

        if (mixWeights[0] > 0.0f)
            outputSample += mixWeights[0] * std::sin(phaseValue * 2.0f * kPi);

        if (mixWeights[1] > 0.0f)
            outputSample += mixWeights[1] * triangle(correctionPhaseValue, colour);

        if (mixWeights[2] > 0.0f)
            outputSample += mixWeights[2] * saw(correctionPhaseValue, colour);

        if (mixWeights[3] > 0.0f)
            outputSample += mixWeights[3] * pulse(correctionPhaseValue, colour);

        return outputSample;
    }

  private:
    float saw(float phaseValue, float colour) const
    {
        const float value = 2.0f * phaseValue - 1.0f - polyBlep(phaseValue, increment);

        // Colour brightens the saw by adding an octave up partial.
        const float doublePhase = frac01(phaseValue + phaseValue);
        const float secondValue = 2.0f * doublePhase - 1.0f - polyBlep(doublePhase, increment * 2.0f);

        return value + colour * 0.35f * secondValue;
    }

    float pulse(float phaseValue, float colour) const
    {
        const float pulseWidth = 0.1f + 0.8f * clampf(colour, 0.0f, 1.0f);
        const float risingEdge = 2.0f * phaseValue - 1.0f - polyBlep(phaseValue, increment);
        const float shiftedPhase = frac01(phaseValue + 1.0f - pulseWidth);
        const float fallingEdge = 2.0f * shiftedPhase - 1.0f - polyBlep(shiftedPhase, increment);

        return risingEdge - fallingEdge;
    }

    float triangle(float phaseValue, float colour) const
    {
        // Built from the band limited saw: the kinks land where the saw's
        // discontinuities are, so the correction carries over.
        const float sawValue = 2.0f * phaseValue - 1.0f - polyBlep(phaseValue, increment);
        const float triangleValue = 1.0f - 2.0f * std::abs(sawValue);

        // The same octave up partial the saw uses, so morphing towards a
        // triangle keeps the brightness of the waveform it came from.
        const float doublePhase = frac01(phaseValue + phaseValue);
        const float secondSawValue = 2.0f * doublePhase - 1.0f - polyBlep(doublePhase, increment * 2.0f);

        return triangleValue + colour * 0.25f * (1.0f - 2.0f * std::abs(secondSawValue));
    }

    float increment = 0.0f;
    float phase = 0.0f;
    float heldSample = 0.0f;
};

} // namespace gmosh
