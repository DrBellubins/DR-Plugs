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

inline float softClip(float input)
{
    // Cheap odd symmetric saturator, unity slope at 0, bounded by +-1.
    return input / (1.0f + std::abs(input) * 0.65f);
}

inline float noteToHz(int midiNote, float centsOffset = 0.0f)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f + centsOffset / 100.0f) / 12.0f);
}

//==============================================================================
class OnePole
{
  public:
    void setHz(float hertz, double sampleRate)
    {
        coefficient = 1.0f - std::exp(-2.0f * kPi * std::max(hertz, 10.0f) / static_cast<float>(sampleRate));
    }

    float process(float input)
    {
        state += (input - state) * coefficient;
        return state;
    }

    void reset() { state = 0.0f; }
    float value() const { return state; }

  private:
    float coefficient = 1.0f;
    float state = 0.0f;
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
    void setSampleRate(double newSampleRate)
    {
        sampleRate = static_cast<float>(newSampleRate);
        reset();
    }

    void setCutoffQ(float hertz, float resonance)
    {
        const float nyquistHz = sampleRate * 0.5f;
        const float clampedCutoffHz = std::min(std::max(hertz, 10.0f), nyquistHz * 0.96f);
        const float clampedResonance = std::max(resonance, 0.3f);

        integratorGain = std::tan(kPi * clampedCutoffHz / sampleRate);
        damping = 1.0f / clampedResonance;

        const float gainTimesDamping = integratorGain * damping;
        inverseLoopGain = 1.0f / (1.0f + gainTimesDamping);
        inverseDenominator = 1.0f / (1.0f + gainTimesDamping + integratorGain * integratorGain);
        lowpassNumerator = 1.0f + gainTimesDamping;
        bandpassNumerator = integratorGain * (2.0f + gainTimesDamping);
        inputNumerator = integratorGain * integratorGain;
    }

    void setDrive(float newDrive) { drive = newDrive; }

    void reset()
    {
        lowpass = 0.0f;
        bandpass = 0.0f;
        previousHighpass = 0.0f;
    }

    /// The three weights select the lowpass, bandpass and highpass outputs.
    float process(float input, float lowpassMix, float bandpassMix, float highpassMix)
    {
        const float inputPlusHistory = input + previousHighpass;

        const float newLowpass =
            (lowpass * lowpassNumerator + bandpass * bandpassNumerator + inputPlusHistory * inputNumerator)
            * inverseDenominator;
        const float newBandpass =
            (bandpass + inputPlusHistory * integratorGain - newLowpass * integratorGain) * inverseLoopGain;
        float newHighpass = input - newBandpass * damping - newLowpass;

        if (drive > 0.0f)
            newHighpass = newHighpass / (1.0f + drive * newHighpass * newHighpass * 0.5f);

        lowpass = newLowpass;
        bandpass = newBandpass;
        previousHighpass = newHighpass;

        return newLowpass * lowpassMix + newBandpass * bandpassMix + newHighpass * highpassMix;
    }

  private:
    float sampleRate = 44100.0f;

    // The solved, delay free loop of the derivation above: integratorGain is the
    // g of that derivation, damping is 1/Q, and the numerator terms are the
    // closed form of one sample step.
    float integratorGain = 0.0f;
    float damping = 0.0f;
    float inverseLoopGain = 1.0f;
    float inverseDenominator = 1.0f;
    float lowpassNumerator = 1.0f;
    float bandpassNumerator = 0.0f;
    float inputNumerator = 0.0f;

    float drive = 0.0f;

    // The three filter states, one per mode.
    float lowpass = 0.0f;
    float bandpass = 0.0f;
    float previousHighpass = 0.0f;
};

//==============================================================================
class DcBlocker
{
  public:
    void setSampleRate(double newSampleRate)
    {
        // One pole highpass at ~7 Hz: r = 1 - 2*pi*fc/fs.
        poleCoefficient = 1.0f - 2.0f * kPi * 7.0f / static_cast<float>(newSampleRate);
    }

    float process(float input)
    {
        const float output = input - previousInput + poleCoefficient * previousOutput;

        previousInput = input;
        previousOutput = output;

        return output;
    }

    void reset()
    {
        previousInput = 0.0f;
        previousOutput = 0.0f;
    }

  private:
    float poleCoefficient = 0.995f;
    float previousInput = 0.0f;
    float previousOutput = 0.0f;
};

//==============================================================================
/// Fractional delay line, linear interpolation, preallocated.
class VariableDelay
{
  public:
    void Prepare(int maxSamples)
    {
        buffer.assign(static_cast<std::size_t>(maxSamples) + 1, 0.0f);
        writePosition = 0;
    }

    void Reset()
    {
        for (float& slot : buffer)
            slot = 0.0f;

        writePosition = 0;
    }

    void Push(float input)
    {
        buffer[writePosition] = input;

        if (++writePosition >= buffer.size())
            writePosition = 0;
    }

    /// delay in samples (>= 1).
    float Read(float delay) const
    {
        const float numSamples = static_cast<float>(Size());
        const float clampedDelay =
            delay < 1.0f ? 1.0f : (delay > numSamples - 2.0f ? numSamples - 2.0f : delay);

        const float reversePos = static_cast<float>(writePosition) - clampedDelay;
        const float indexBelowPos = std::floor(reversePos);
        const float fraction = reversePos - indexBelowPos;
        const auto indexBelow = static_cast<std::size_t>(wrap(indexBelowPos, numSamples));
        const auto indexAbove = static_cast<std::size_t>(wrap(indexBelowPos + 1.0f, numSamples));

        return buffer[indexBelow] * (1.0f - fraction) + buffer[indexAbove] * fraction;
    }

    int Size() const { return static_cast<int>(buffer.size()); }

  private:
    static float wrap(float value, float numSamples)
    {
        while (value < 0.0f)
            value += numSamples;

        while (value >= numSamples)
            value -= numSamples;

        return value;
    }

    std::vector<float> buffer;
    std::size_t writePosition = 0;
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
    // straight ramp and the voice into a click. The exponent stays below 1 so
    // the drop is fast and the tail stays long.
    static constexpr float kPluckFallExponent = 0.6f;

    // Both curves are evaluated through a table of this size, built in
    // setShape(), so the audio loop only ever reads it.
    static constexpr int kTableSize = 512;

    struct Shape
    {
        float Period = 1.0f; ///< pluck period in seconds (<0 = one very long pluck)
        float PeakPos = 0.06f; ///< where the pluck peaks, as a fraction of the period
        float RiseExp = 1.0f; ///< curvature of the rise into the peak (1 = linear)
        float FallExp = kPluckFallExponent;
        float Duration = 0.0f; ///< outer decay time; <= 0 = no outer decay
        float OuterExp = 1.0f; ///< curvature of the outer decay (1 = linear)
        float FloorLevel = 0.0f; ///< where the outer decay settles
        float Release = 0.25f; ///< note off fade, from the level it is at
        float ModPeak = 0.06f; ///< the modulator's own peak position
        float ModFallScale = 1.0f; ///< fraction of the level's fall window the mod uses
    };

    void setSampleRate(double newSampleRate) { sampleRate = static_cast<float>(newSampleRate); }

    void setShape(const Shape& newShape)
    {
        shape = newShape;

        // A period of about an hour is "one pluck that never gets to repeat";
        // anything faster than 2 ms is treated as an audio rate tremolo.
        const float period = (newShape.Period > 0.002f ? newShape.Period : 3600.0f);
        phaseIncrement = 1.0f / (period * sampleRate);

        outerIncrement = (newShape.Duration > 0.0f) ? 1.0f / (newShape.Duration * sampleRate) : 0.0f;
        releaseCoefficient = 1.0f - std::exp(-1.0f / (std::max(newShape.Release, 0.01f) * sampleRate));

        buildTable(levelCurve, newShape.PeakPos, newShape.RiseExp, newShape.FallExp, 1.0f);
        buildTable(modulatorCurve, newShape.ModPeak, newShape.RiseExp, newShape.FallExp,
                   newShape.ModFallScale);
    }

    void gateOn()
    {
        phase = 0.0f;
        outerPosition = 0.0f;
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
            amplitude = 0.0f;
            modulation = 0.0f;
            return;
        }

        phase += phaseIncrement;

        if (phase >= 1.0f)
            phase -= 1.0f;

        if (outerIncrement > 0.0f)
        {
            outerPosition += outerIncrement;

            if (outerPosition > 1.0f)
                outerPosition = 1.0f;
        }

        if (stage == Releasing)
        {
            gateLevel -= gateLevel * releaseCoefficient;

            if (gateLevel < 1.0e-4f)
            {
                gateLevel = 0.0f;
                stage = Idle;
            }
        }

        const float outer = outerLevel();
        amplitude = readTable(levelCurve, phase) * outer * gateLevel;
        modulation = readTable(modulatorCurve, phase) * outer * gateLevel;

        // A note with nothing to settle to ends by itself. The outer curve is
        // exactly zero at that point, so this cannot click.
        if (stage == Running && outerPosition >= 1.0f && shape.FloorLevel <= 1.0e-3f)
            stage = Idle;
    }

    float amplitudeValue() const { return amplitude; }
    float modulationValue() const { return modulation; }

    /// The outer curve on its own: a slow modulator that follows the note as a
    /// whole instead of following every pluck.
    float outerValue() const { return outerLevel() * gateLevel; }

    bool isIdle() const { return stage == Idle; }

    void reset()
    {
        stage = Idle;
        amplitude = 0.0f;
        modulation = 0.0f;
        gateLevel = 0.0f;
        phase = 0.0f;
        outerPosition = 0.0f;
    }

  private:
    static float curveAt(float phaseValue, float peakPosition, float riseExponent, float fallExponent,
                         float fallWindow)
    {
        if (phaseValue <= 0.0f)
            return 0.0f;

        if (phaseValue < peakPosition)
        {
            const float risePosition = phaseValue / peakPosition;
            return std::pow(risePosition, riseExponent);
        }

        const float window = (1.0f - peakPosition) * std::max(fallWindow, 0.0f);

        if (window <= 1.0e-6f)
            return 0.0f;

        const float fallPosition = (phaseValue - peakPosition) / window;

        if (fallPosition >= 1.0f)
            return 0.0f;

        return std::pow(1.0f - fallPosition, fallExponent);
    }

    void buildTable(float* table, float peakPosition, float riseExponent, float fallExponent,
                    float fallWindow)
    {
        peakPosition = clampf(peakPosition, 0.005f, 0.98f);

        for (int entryIndex = 0; entryIndex < kTableSize; ++entryIndex)
        {
            const float phaseValue = static_cast<float>(entryIndex) / static_cast<float>(kTableSize - 1);
            table[entryIndex] = curveAt(phaseValue, peakPosition, riseExponent, fallExponent, fallWindow);
        }
    }

    static float readTable(const float* table, float phaseValue)
    {
        const float clampedPhase = clamp01(phaseValue);
        const float position = clampedPhase * static_cast<float>(kTableSize - 1);
        const float positionFloor = std::floor(position);
        const int indexBelow = static_cast<int>(positionFloor);
        const int indexAbove = indexBelow + 1 < kTableSize ? indexBelow + 1 : indexBelow;
        const float fraction = position - positionFloor;

        return table[indexBelow] + (table[indexAbove] - table[indexBelow]) * fraction;
    }

    float outerLevel() const
    {
        if (outerIncrement <= 0.0f)
            return 1.0f;

        const float floorLevel = clamp01(shape.FloorLevel);

        if (outerPosition <= 0.0f)
            return 1.0f;

        if (outerPosition >= 1.0f)
            return floorLevel;

        // Counted from the top down: (1 - q) is exact at both ends, and a floor
        // of zero lands on exactly zero rather than on a rounding error.
        return floorLevel + (1.0f - floorLevel) * std::pow(1.0f - outerPosition, shape.OuterExp);
    }

    enum Stage : unsigned char
    {
        Idle,
        Running,
        Releasing
    };

    float sampleRate = 44100.0f;

    float phaseIncrement = 0.0f;
    float outerIncrement = 0.0f;
    float releaseCoefficient = 0.01f;

    float phase = 0.0f;
    float outerPosition = 0.0f;
    float gateLevel = 0.0f;

    float amplitude = 0.0f;
    float modulation = 0.0f;

    Shape shape;
    Stage stage = Idle;

    float levelCurve[kTableSize]{};
    float modulatorCurve[kTableSize]{};
};

//==============================================================================
/// Sample and hold on an existing signal. Used on the modulator (mod_sh), which
/// turns "a pluck following the envelope" into "a squared envelope that still
/// follows the envelope". The step is the sound, so nothing here smooths it.
class SampleHold
{
  public:
    void setSampleRate(double newSampleRate) { sampleRate = static_cast<float>(newSampleRate); }

    /// 0 Hz means pass through.
    void setRateHz(float hertz) { increment = hertz > 0.0f ? std::min(hertz / sampleRate, 0.5f) : 0.0f; }

    float process(float input)
    {
        if (increment <= 0.0f)
            return input;

        phase += increment;

        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            value = input;
        }

        return value;
    }

    void reset()
    {
        phase = 0.0f;
        value = 0.0f;
    }

  private:
    float sampleRate = 44100.0f;
    float increment = 0.0f;
    float phase = 0.0f;
    float value = 0.0f;
};

//==============================================================================
/// Free running modulator: sine / triangle / sample and hold, chosen per voice.
class Modulator
{
  public:
    void setRateHz(float hertz, double sampleRate)
    {
        increment = std::min(hertz, static_cast<float>(sampleRate) * 0.25f) / static_cast<float>(sampleRate);
    }

    void setShape(int shape, Rng& randomGenerator)
    {
        waveform = static_cast<unsigned char>(shape % 3);
        holdValue = 0.0f;
        holdPhase = randomGenerator.uniform();
    }

    void setPhase(float newPhase) { phase = newPhase; }

    float process()
    {
        phase += increment;

        if (phase >= 1.0f)
        {
            phase -= 1.0f;

            // The step is drawn again once per period, so a triangle or sine
            // never sees a discontinuity here.
            if (waveform == 2)
                holdPhase = holdValue;
        }

        switch (waveform)
        {
            case 1:
                // triangle
                return phase < 0.5f ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);

            case 2:

                // sample and hold
                if (phase < increment)
                    holdValue = holdPhase * 2.0f - 1.0f;

                return holdValue;

            case 0:
            default:
                // sine
                return std::sin(phase * 2.0f * kPi);
        }
    }

    void reset() { phase = 0.0f; }

  private:
    float increment = 0.0f;
    float phase = 0.0f;
    float holdValue = 0.0f;
    float holdPhase = 0.0f;
    unsigned char waveform = 0;
};

//==============================================================================
/// Slow smoothed random walk: the "drift" that makes a patch feel alive
/// (design doc 7, voice level life).
class Drift
{
  public:
    void prepare(double sampleRate, Rng& randomGenerator, float rateHertz)
    {
        interval = static_cast<int>(sampleRate / std::max(rateHertz, 0.02f));
        smoothCoefficient = 1.0f - std::exp(-1.0f / (0.25f * static_cast<float>(sampleRate)));
        target = randomGenerator.range(-1.0f, 1.0f);
        counter = randomGenerator.integer(interval) + 1;
        value = target;
    }

    float process(Rng& randomGenerator)
    {
        if (--counter <= 0)
        {
            counter = interval;
            target = randomGenerator.gaussianClamped(1.0f);
        }

        value += (target - value) * smoothCoefficient;
        return value;
    }

    float valueOf() const { return value; }

  private:
    int interval = 1000;
    int counter = 1000;
    float smoothCoefficient = 0.001f;
    float target = 0.0f;
    float value = 0.0f;
};

} // namespace gmosh
