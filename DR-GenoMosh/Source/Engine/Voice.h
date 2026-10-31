#pragma once

#include "Dsp.h"
#include "Oscillator.h"
#include "VoiceParams.h"

namespace gmosh
//==============================================================================
// One synth voice (design doc section 7).
//
//   note -> [Osc A] ---+
//          [Osc B] ---+-> balance -> dual filter -> amp env -> pan
//          [Sub]  ---+
//
// Modulation: one shared envelope (a pluck with a level decay over its
// repeats) drives the amp and, on the same clock, the modulation destinations.
// The LFO and the two slow drift generators run free, plus per voice random
// detune and phase start.
//
// Pitch policy (design doc 5.3): the played pitch is the note plus the a_freq /
// b_freq offset genes, then the mod envelope's bounded dive or rise (a_mod /
// b_mod, hard capped in VoiceParams), vibrato from the LFO and a few cents of
// drift. Everything that can move the pitch has a ceiling the voice cannot
// exceed; only the drift and detune are always tiny.
//==============================================================================
{
class Voice
{
  public:
    void prepare(double sampleRateHz, int maxBlockSamples);

    /// Capture the voice settings at note on (design doc section 3: a voice
    /// holds its own snapshot, so growing a branch does not click held notes).
    void start(const VoiceParams& newParams, int midiNote, uint64_t voiceHash);

    void release();

    void render(float* outL, float* outR, int numSamples);

    bool isActive() const { return active; }
    void clearActive() { active = false; }
    int noteNumber() const { return note; }
    float currentPeak() const { return peak; }
    std::int64_t startSample() const { return startedAt; }
    bool isFinished() const { return envelope.isIdle(); }

    /// A slow peak follower, not the instantaneous level: with a pluck envelope
    /// the voice sits at zero twice per cycle, and a voice must not be stolen
    /// just because the note on it happens to be between two plucks.
    float ampLevel() const { return levelTrack; }

  private:
    float whiteNoise();

    /// The modulation sources at this point in the note, expressed the way the
    /// control rate loop wants them.
    void updateControlTargets(float modulationValue, float lfoValue, float driftPitchValue,
                              float driftFilterValue);

    // Control rate: modulation targets are evaluated every kCtrlPeriod samples
    // and ramped between (design doc 6.4).
    enum
    {
        kCtrlPeriod = 16
    };

    int ctrlCountdown = 0;

    // Every modulation destination is stored as a current value, a target and
    // the slope between them.
    float frequencyA = 0.0f;
    float frequencyATarget = 0.0f;
    float frequencyASlope = 0.0f;

    float frequencyB = 0.0f;
    float frequencyBTarget = 0.0f;
    float frequencyBSlope = 0.0f;

    float colourA = 0.0f;
    float colourATarget = 0.0f;
    float colourASlope = 0.0f;

    float colourB = 0.0f;
    float colourBTarget = 0.0f;
    float colourBSlope = 0.0f;

    float cutoffHzA = 0.0f;
    float cutoffHzATarget = 0.0f;
    float cutoffHzASlope = 0.0f;

    float cutoffHzB = 0.0f;
    float cutoffHzBTarget = 0.0f;
    float cutoffHzBSlope = 0.0f;

    float fmIndex = 0.0f;
    float fmIndexTarget = 0.0f;
    float fmIndexSlope = 0.0f;

    double sampleRate = 44100.0;

    VoiceParams params;
    int note = 60;
    bool active = false;
    std::int64_t startedAt = 0;
    std::int64_t sampleCount = 0;
    float peak = 0.0f;

    MorphOsc oscillatorA;
    MorphOsc oscillatorB;
    float subPhase = 0.0f;
    float subIncrement = 0.0f;

    SharedEnv envelope;
    SampleHold modHold;
    Modulator lfo;
    Drift driftPitch;
    Drift driftFilter;
    Rng randomGenerator{ 0 };

    TrapezoidSVF filterA;
    TrapezoidSVF filterB;
    DcBlocker dcBlocker;

    float baseFrequencyA = 220.0f;
    float baseFrequencyB = 220.0f;
    float baseFrequencySub = 110.0f;

    float detuneA = 0.0f;
    float detuneB = 0.0f;

    float panGainL = 0.7f;
    float panGainR = 0.7f;

    float noiseState = 0.0f;

    float levelTrack = 0.0f;
    float levelDecayCoefficient = 0.999f;
};

} // namespace gmosh
