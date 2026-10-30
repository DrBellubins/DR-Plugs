#pragma once

#include "../Genetic/Curves.h"
#include "../Genetic/Rng.h"

#include <cmath>
#include <vector>

namespace gmosh
{
//==============================================================================
// Small DSP building blocks used by the voice and the effects.
// All of them are allocation free after prepare() and sample rate aware
// (design doc section 10).
//==============================================================================

constexpr float kPi = 3.14159265358979323846f;

inline float softClip (float x)
{
    // Cheap odd symmetric saturator, unity slope at 0, bounded by +-1.
    return x / (1.0f + std::abs (x) * 0.65f);
}

inline float noteToHz (int midiNote, float centsOffset = 0.0f)
{
    return 440.0f * std::pow (2.0f, (static_cast<float> (midiNote) - 69.0f + centsOffset / 100.0f) / 12.0f);
}

//==============================================================================
class OnePole
{
public:
    void setHz (float hz, double sampleRate)
    {
        coef = 1.0f - std::exp (-2.0f * kPi * std::max (hz, 10.0f) / static_cast<float> (sampleRate));
    }

    float process (float x)
    {
        state += (x - state) * coef;
        return state;
    }

    void reset() { state = 0.0f; }
    float value() const { return state; }

private:
    float coef = 1.0f, state = 0.0f;
};

//==============================================================================
/// Trapezoidal (zero delay feedback) state variable filter.
///
/// Derived by digitising the classic coupled integrator SVF with the trapezoidal
/// rule and solving the resulting delay free loop:
///
///   low  = L + g*(band + B)
///   band = B + g*(hp + hpPrev)
///   hp   = x - band/Q - low
///
/// with g = tan(pi*fc/fs). Unlike the plain Chamberlin form this stays stable
/// and accurate right up to Nyquist, which matters because flt_freq goes to
/// 18 kHz.
class TrapezoidSVF
{
public:
    void setSampleRate (double sampleRate) { sr = static_cast<float> (sampleRate); reset(); }

    void setCutoffQ (float hz, float q)
    {
        const float nyq = sr * 0.5f;
        const float fc = std::min (std::max (hz, 10.0f), nyq * 0.96f);
        const float qq = std::max (q, 0.3f);

        g = std::tan (kPi * fc / sr);
        k = 1.0f / qq;

        const float gk = g * k;
        invGk = 1.0f / (1.0f + gk);
        invD  = 1.0f / (1.0f + gk + g * g);
        cL    = 1.0f + gk;
        cB    = g * (2.0f + gk);
        cX    = g * g;
    }

    void setDrive (float d) { drive = d; }

    void reset() { low = band = hpPrev = 0.0f; }

    /// modeMix: (lp, bp, hp) weights.
    float process (float x, float lpMix, float bpMix, float hpMix)
    {
        const float xh = x + hpPrev;

        float newLow  = (low * cL + band * cB + xh * cX) * invD;
        float newBand = (band + xh * g - newLow * g) * invGk;
        float newHp   = x - newBand * k - newLow;

        if (drive > 0.0f)
            newHp = newHp / (1.0f + drive * newHp * newHp * 0.5f);

        low = newLow;
        band = newBand;
        hpPrev = newHp;

        return newLow * lpMix + newBand * bpMix + newHp * hpMix;
    }

private:
    float sr = 44100.0f;
    float g = 0.0f, k = 0.0f, invGk = 1.0f, invD = 1.0f, cL = 1.0f, cB = 0.0f, cX = 0.0f;
    float drive = 0.0f;
    float low = 0.0f, band = 0.0f, hpPrev = 0.0f;
};

//==============================================================================
class DcBlocker
{
public:
    void setSampleRate (double sampleRate)
    {
        // One pole highpass at ~7 Hz: r = 1 - 2*pi*fc/fs.
        r = 1.0f - 2.0f * kPi * 7.0f / static_cast<float> (sampleRate);
    }

    float process (float x)
    {
        const float y = x - prevIn + r * prevOut;
        prevIn = x;
        prevOut = y;
        return y;
    }

    void reset() { prevIn = prevOut = 0.0f; }

private:
    float r = 0.995f, prevIn = 0.0f, prevOut = 0.0f;
};

//==============================================================================
/// Fractional delay line, linear interpolation, preallocated.
class VariableDelay
{
public:
    void prepare (int maxSamples)
    {
        buffer.assign (static_cast<std::size_t> (maxSamples) + 1, 0.0f);
        writePos = 0;
    }

    void reset() { for (float& s : buffer) s = 0.0f; writePos = 0; }

    void push (float x)
    {
        buffer[writePos] = x;
        if (++writePos >= buffer.size())
            writePos = 0;
    }

    /// delay in samples (>= 1).
    float read (float delay) const
    {
        const float n = static_cast<float> (size());
        float d = delay < 1.0f ? 1.0f : (delay > n - 2.0f ? n - 2.0f : delay);

        const float rp = static_cast<float> (writePos) - d;
        float i0f = std::floor (rp);
        const float frac = rp - i0f;
        auto i0 = static_cast<std::size_t> (wrap (i0f, n));
        auto i1 = static_cast<std::size_t> (wrap (i0f + 1.0f, n));

        return buffer[i0] * (1.0f - frac) + buffer[i1] * frac;
    }

    int size() const { return static_cast<int> (buffer.size()); }

private:
    static float wrap (float v, float n)
    {
        while (v < 0.0f) v += n;
        while (v >= n)   v -= n;
        return v;
    }

    std::vector<float> buffer;
    std::size_t writePos = 0;
};

//==============================================================================
// The envelope.
//
// Not an ADSR. The instrument's envelope is *one clock and two curves that are
// multiplied together*:
//
//     amp(t) = pluck (phase)  x  outer (t / duration)  x  gate
//     mod(t) = pluckM(phase)  x  outer (t / duration)  x  gate
//
// The inner curve is a pluck: it starts at zero, rises to a single peak and
// falls back to zero. It never plateaus, and it repeats at `period`. Rapidly
// repeating plucks on a held key are the intended sound.
//
// The outer curve is not a second envelope shape, it is a level decay applied
// to the whole train of plucks: it falls from 1.0 to `floor` over `duration`
// seconds. `duration <= 0` means no decay at all, i.e. the pluck train runs at
// full level until the key is released.
//
// The modulator is the *same* curve on the *same* clock, with its own peak
// position and its own fall window; it has no shape of its own. Everything the
// mapping layer needs to decide is in Shape; this class derives nothing.
//
// Both curves are evaluated through a small wavetable built when the voice is
// started, so the audio loop does no pow() and moving a parameter never clicks
// mid note (a fresh note is always built from scratch).
//==============================================================================
class SharedEnv
{
public:
    // The pluck's fall is deliberately not a parameter: that curve is what
    // makes the shape a pluck. Giving it a gene is what turns it into a
    // straight ramp and the voice into a click.
    static constexpr float kPluckFallExponent = 0.6f;   // <1 = fast drop, long tail
    static constexpr int   kTableSize = 512;

    struct Shape
    {
        float period = 1.0f;        ///< pluck period in seconds (<0 = one very long pluck)
        float peakPos = 0.06f;      ///< where the pluck peaks, as a fraction of the period
        float riseExp = 1.0f;       ///< curvature of the rise into the peak (1 = linear)
        float fallExp = kPluckFallExponent;
        float duration = 0.0f;      ///< outer decay time; <= 0 = no outer decay
        float outerExp = 1.0f;      ///< curvature of the outer decay (1 = linear)
        float floorLevel = 0.0f;    ///< where the outer decay settles
        float release = 0.25f;      ///< note off fade, from the level it is at
        float modPeak = 0.06f;      ///< the modulator's own peak position
        float modFallScale = 1.0f;  ///< fraction of the level's fall window the mod uses
    };

    void setSampleRate (double sampleRate) { sr = static_cast<float> (sampleRate); }

    void setShape (const Shape& s)
    {
        shape = s;

        // A period of about an hour is "one pluck that never gets to repeat";
        // anything faster than 2 ms is treated as an audio rate tremolo.
        const float period = (s.period > 0.002f ? s.period : 3600.0f);
        phaseInc = 1.0f / (period * sr);

        outerInc = (s.duration > 0.0f) ? 1.0f / (s.duration * sr) : 0.0f;
        releaseCoef = 1.0f - std::exp (-1.0f / (std::max (s.release, 0.01f) * sr));

        buildTable (curve, s.peakPos, s.riseExp, s.fallExp, 1.0f);
        buildTable (modCurve, s.modPeak, s.riseExp, s.fallExp, s.modFallScale);
    }

    void gateOn()
    {
        phase = 0.0f;
        outerPos = 0.0f;
        gateLevel = 1.0f;
        stage = Running;
    }

    void gateOff()
    {
        if (stage == Running)
            stage = Releasing;
    }

    void process()
    {
        if (stage == Idle)
        {
            amp = mod = 0.0f;
            return;
        }

        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        if (outerInc > 0.0f)
        {
            outerPos += outerInc;
            if (outerPos > 1.0f)
                outerPos = 1.0f;
        }

        if (stage == Releasing)
        {
            gateLevel -= gateLevel * releaseCoef;
            if (gateLevel < 1.0e-4f)
            {
                gateLevel = 0.0f;
                stage = Idle;
            }
        }

        const float outer = outerOf();
        amp = readTable (curve, phase) * outer * gateLevel;
        mod = readTable (modCurve, phase) * outer * gateLevel;

        // A note with nothing to settle to ends by itself. The outer curve is
        // exactly zero at that point, so this cannot click.
        if (stage == Running && outerPos >= 1.0f && shape.floorLevel <= 1.0e-3f)
            stage = Idle;
    }

    float ampValue() const { return amp; }
    float modValue() const { return mod; }

    /// The outer curve on its own: a slow modulator that follows the note as a
    /// whole instead of following every pluck.
    float outerValue() const { return outerOf() * gateLevel; }

    bool isIdle() const { return stage == Idle; }
    void reset() { stage = Idle; amp = mod = 0.0f; gateLevel = 0.0f; phase = 0.0f; outerPos = 0.0f; }

private:
    static float curveAt (float ph, float peak, float riseExp, float fallExp, float fallWindow)
    {
        if (ph <= 0.0f)
            return 0.0f;
        if (ph < peak)
        {
            const float u = ph / peak;
            return std::pow (u, riseExp);
        }
        const float window = (1.0f - peak) * std::max (fallWindow, 0.0f);
        if (window <= 1.0e-6f)
            return 0.0f;
        const float w = (ph - peak) / window;
        if (w >= 1.0f)
            return 0.0f;
        return std::pow (1.0f - w, fallExp);
    }

    void buildTable (float* t, float peak, float riseExp, float fallExp, float fallWindow)
    {
        peak = clampf (peak, 0.005f, 0.98f);
        for (int i = 0; i < kTableSize; ++i)
            t[i] = curveAt (static_cast<float> (i) / static_cast<float> (kTableSize - 1),
                            peak, riseExp, fallExp, fallWindow);
    }

    static float readTable (const float* t, float ph)
    {
        ph = clamp01 (ph);
        const float pos = ph * static_cast<float> (kTableSize - 1);
        const float posFloor = std::floor (pos);
        const int i0 = static_cast<int> (posFloor);
        const int i1 = i0 + 1 < kTableSize ? i0 + 1 : i0;
        const float frac = pos - posFloor;
        return t[i0] + (t[i1] - t[i0]) * frac;
    }

    float outerOf() const
    {
        if (outerInc <= 0.0f)
            return 1.0f;
        const float floorL = clamp01 (shape.floorLevel);
        if (outerPos <= 0.0f)
            return 1.0f;
        if (outerPos >= 1.0f)
            return floorL;
        // Counted from the top down: (1 - q) is exact at both ends, and a floor
        // of zero lands on exactly zero rather than on a rounding error.
        return floorL + (1.0f - floorL) * std::pow (1.0f - outerPos, shape.outerExp);
    }

    enum Stage : unsigned char { Idle, Running, Releasing };

    Shape shape;
    float sr = 44100.0f;
    float phaseInc = 0.0f, outerInc = 0.0f, releaseCoef = 0.01f;
    float phase = 0.0f, outerPos = 0.0f, gateLevel = 0.0f;
    float amp = 0.0f, mod = 0.0f;
    Stage stage = Idle;

    float curve[kTableSize] {};
    float modCurve[kTableSize] {};
};

//==============================================================================
/// Sample and hold on an existing signal. Used on the modulator (mod_sh), which
/// turns "a pluck following the envelope" into "a squared envelope that still
/// follows the envelope". The step is the sound, so nothing here smooths it.
class SampleHold
{
public:
    void setSampleRate (double sampleRate) { sr = static_cast<float> (sampleRate); }

    /// 0 Hz means pass through.
    void setRateHz (float hz)
    {
        inc = hz > 0.0f ? std::min (hz / sr, 0.5f) : 0.0f;
    }

    float process (float x)
    {
        if (inc <= 0.0f)
            return x;
        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            value = x;
        }
        return value;
    }

    void reset() { phase = 0.0f; value = 0.0f; }

private:
    float sr = 44100.0f, inc = 0.0f, phase = 0.0f, value = 0.0f;
};

//==============================================================================
/// Free running modulator: sine / triangle / sample and hold, chosen per voice.
class Modulator
{
public:
    void setRateHz (float hz, double sampleRate)
    {
        inc = std::min (hz, static_cast<float> (sampleRate) * 0.25f) / static_cast<float> (sampleRate);
    }

    void setShape (int shape, Rng& rng)
    {
        wave = static_cast<unsigned char> (shape % 3);
        holdValue = 0.0f;
        holdPhase = rng.uniform();
    }

    void setPhase (float p) { phase = p; }

    float process()
    {
        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            if (wave == 2) holdPhase = holdValue; // re-randomise per period
        }

        switch (wave)
        {
            case 1: // triangle
                return phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
            case 2: // sample and hold
                if (phase < inc) holdValue = holdPhase * 2.0f - 1.0f;
                return holdValue;
            case 0:
            default: // sine
                return std::sin (phase * 2.0f * kPi);
        }
    }

    void reset() { phase = 0.0f; }

private:
    float inc = 0.0f, phase = 0.0f, holdValue = 0.0f, holdPhase = 0.0f;
    unsigned char wave = 0;
};

//==============================================================================
/// Slow smoothed random walk: the "drift" that makes a patch feel alive
/// (design doc 7, voice level life).
class Drift
{
public:
    void prepare (double sampleRate, Rng& rng, float rateHz)
    {
        interval = static_cast<int> (sampleRate / std::max (rateHz, 0.02f));
        smooth = 1.0f - std::exp (-1.0f / (0.25f * static_cast<float> (sampleRate)));
        target = rng.range (-1.0f, 1.0f);
        counter = rng.integer (interval) + 1;
        value = target;
    }

    float process (Rng& rng)
    {
        if (--counter <= 0)
        {
            counter = interval;
            target = rng.gaussianClamped (1.0f);
        }
        value += (target - value) * smooth;
        return value;
    }

    float valueOf() const { return value; }

private:
    int interval = 1000, counter = 1000;
    float smooth = 0.001f, target = 0.0f, value = 0.0f;
};

} // namespace gmosh
