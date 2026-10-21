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

/// PolyBLEP correction (Välimäki/Stiles). t is the phase since the last
/// discontinuity in [0,1), dt is the phase increment.
inline float polyBlep (float t, float dt)
{
    if (dt <= 0.0f)
        return 0.0f;

    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }

    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }

    return 0.0f;
}

inline float frac01 (float v)
{
    v = v - std::floor (v);
    return v < 0.0f ? v + 1.0f : v;
}

class MorphOsc
{
public:
    void setFrequency (float hz, float sampleRate)
    {
        inc = hz / sampleRate;
        if (inc < 0.0f)   inc = 0.0f;
        if (inc > 0.45f)  inc = 0.45f;
    }

    void setPhase (float p) { phase = frac01 (p); }
    float phaseValue() const { return phase; }
    float increment() const { return inc; }
    void reset() { phase = 0.0f; }

    /// Advance the phase, then render. form: 0 sine, 1/3 triangle, 2/3 saw,
    /// 1 pulse. colour: pulse width / spectral tilt.
    float next (float form, float colour)
    {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return render (phase, form, colour);
    }

    /// Render at a phase modulated copy of the current phase (FM/PM).
    float nextModulated (float form, float colour, float phaseMod)
    {
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        return render (frac01 (phase + phaseMod), form, colour, phase);
    }

    /// Sample and hold output: the value the oscillator had at the start of the
    /// current period (used by the "B shape" structural mutation).
    float heldValue (float form, float colour)
    {
        if (phase < inc)
            hold = render (phase, form, colour);
        return hold;
    }

    float render (float p, float form, float colour, float correctionPhase = -1.0f) const
    {
        const float cp = correctionPhase >= 0.0f ? correctionPhase : p;

        float w[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        const float seg = clampf (form, 0.0f, 1.0f) * 3.0f;
        int idx = static_cast<int> (seg);
        if (idx > 2) idx = 2;
        const float f = seg - static_cast<float> (idx);
        w[idx]     = std::sqrt (1.0f - f);
        w[idx + 1] = std::sqrt (f);

        float out = 0.0f;
        if (w[0] > 0.0f) out += w[0] * std::sin (p * 2.0f * kPi);
        if (w[1] > 0.0f) out += w[1] * triangle (cp, colour);
        if (w[2] > 0.0f) out += w[2] * saw (cp, colour);
        if (w[3] > 0.0f) out += w[3] * pulse (cp, colour);
        return out;
    }

private:
    float saw (float p, float colour) const
    {
        const float v = 2.0f * p - 1.0f - polyBlep (p, inc);
        // "colour" brightens by adding an octave up partial.
        const float p2 = frac01 (p + p);
        const float v2 = 2.0f * p2 - 1.0f - polyBlep (p2, inc * 2.0f);
        return v + colour * 0.35f * v2;
    }

    float pulse (float p, float colour) const
    {
        const float pw = 0.1f + 0.8f * clampf (colour, 0.0f, 1.0f);
        const float a = 2.0f * p - 1.0f - polyBlep (p, inc);
        const float q = frac01 (p + 1.0f - pw);
        const float b = 2.0f * q - 1.0f - polyBlep (q, inc);
        return a - b;
    }

    float triangle (float p, float colour) const
    {
        // Built from the band limited saw: the kinks land where the saw's
        // discontinuities are, so the correction carries over.
        const float s = 2.0f * p - 1.0f - polyBlep (p, inc);
        const float base = 1.0f - 2.0f * std::abs (s);
        const float p2 = frac01 (p + p);
        const float s2 = 2.0f * p2 - 1.0f - polyBlep (p2, inc * 2.0f);
        return base + colour * 0.25f * (1.0f - 2.0f * std::abs (s2));
    }

    float inc = 0.0f;
    float phase = 0.0f;
    float hold = 0.0f;
};

} // namespace gmosh
