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
    void prepare (double sampleRate, int maxBlockSamples);

    /// Capture the voice settings at note on (design doc section 3: a voice
    /// holds its own snapshot, so growing a branch does not click held notes).
    void start (const VoiceParams& params, int midiNote, uint64_t voiceHash);
    void release();

    void render (float* outL, float* outR, int numSamples);

    bool isActive() const { return active; }
    void clearActive() { active = false; }
    int noteNumber() const { return note; }
    float currentPeak() const { return peak; }
    std::int64_t startSample() const { return startedAt; }
    bool isFinished() const { return env.isIdle(); }

    /// A slow peak follower, not the instantaneous level: with a pluck envelope
    /// the voice sits at zero twice per cycle, and a voice must not be stolen
    /// just because the note on it happens to be between two plucks.
    float ampLevel() const { return levelTrack; }

private:
    float whiteNoise();
    void updateControlTargets (float modValue, float lfoV,
                               float driftP, float driftF);

    // Control rate: modulation targets are evaluated every kCtrlPeriod samples
    // and ramped between (design doc 6.4).
    enum { kCtrlPeriod = 16 };
    int ctrlCountdown = 0;
    float freqA = 0.0f, freqATo = 0.0f, freqASlope = 0.0f;
    float freqB = 0.0f, freqBTo = 0.0f, freqBSlope = 0.0f;
    float colourA = 0.0f, colourATo = 0.0f, colourASlope = 0.0f;
    float colourB = 0.0f, colourBTo = 0.0f, colourBSlope = 0.0f;
    float cutoff1 = 0.0f, cutoff1To = 0.0f, cutoff1Slope = 0.0f;
    float cutoff2 = 0.0f, cutoff2To = 0.0f, cutoff2Slope = 0.0f;
    float fmIndex = 0.0f, fmIndexTo = 0.0f, fmIndexSlope = 0.0f;

    double sampleRate = 44100.0;

    VoiceParams p;
    int note = 60;
    bool active = false;
    std::int64_t startedAt = 0;
    std::int64_t sampleCount = 0;
    float peak = 0.0f;

    MorphOsc oscA, oscB;
    float subPhase = 0.0f, subInc = 0.0f;

    SharedEnv env;
    SampleHold modHold;
    Modulator lfo;
    Drift driftPitch, driftFilter;
    Rng rng { 0 };

    TrapezoidSVF filter1, filter2;
    DcBlocker dcBlock;

    float baseFreqA = 220.0f, baseFreqB = 220.0f, baseFreqSub = 110.0f;
    float detuneA = 0.0f, detuneB = 0.0f;
    float panGainL = 0.7f, panGainR = 0.7f;
    float noiseState = 0.0f;
    float levelTrack = 0.0f, levelDecayCoef = 0.999f;
};

} // namespace gmosh
