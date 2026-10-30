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
// Envelopes.
//
// Deliberately built as smoothed first order stages rather than hard ramps:
// they can never click when a parameter moves, which matters because branch
// lengths and the wheel move under a held note.
//==============================================================================
class Env
{
public:
    enum Stage : unsigned char { Idle, Attack, Decay, Release };

    void setSampleRate (double sampleRate) { sr = static_cast<float> (sampleRate); }

    void setTimes (float attack, float decay, float sustain, float release)
    {
        aC = timeCoef (attack);
        dC = timeCoef (decay);
        rC = timeCoef (release);
        sus = sustain;
    }

    void gateOn()
    {
        if (stage != Attack)
            stage = Attack;
    }

    void gateOff()
    {
        if (stage != Idle)
            stage = Release;
    }

    float process()
    {
        switch (stage)
        {
            case Attack:
                value += (1.0f - value) * aC;
                if (value > 0.999f) { value = 1.0f; stage = Decay; }
                break;
            case Decay:
                value += (sus - value) * dC;
                break;
            case Release:
                value += (0.0f - value) * rC;
                if (value * value < 1.0e-7f) { value = 0.0f; stage = Idle; }
                break;
            case Idle:
            default:
                value = 0.0f;
                break;
        }
        return value;
    }

    bool isIdle() const { return stage == Idle; }
    float valueOf() const { return value; }
    void reset() { value = 0.0f; stage = Idle; }

private:
    float timeCoef (float seconds) const
    {
        const float t = seconds < 0.0005f ? 0.0005f : seconds;
        return 1.0f - std::exp (-1.0f / (t * sr));
    }

    float sr = 44100.0f;
    float aC = 1.0f, dC = 1.0f, rC = 1.0f, sus = 0.0f;
    float value = 0.0f;
    Stage stage = Idle;
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
