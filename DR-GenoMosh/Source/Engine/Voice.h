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
// Modulation: the shared envelope of design 6.1.1 (one clock for amp and mod),
// the LFO, and two slow drift generators, all per voice, plus per voice random
// detune and phase start.
//
// What this class does *not* do is arithmetic on genes. Every value it reads
// comes from VoiceParams, which is the one mapping layer (design doc 3, 5.7.2):
// the pitches, both cutoffs, the mix in equal power mix space, the gated levels.
// Re-deriving any of it here is how links end up applied twice and disagree.
//
// Pitch policy (design doc 5.3): the played pitch is the note plus the
// resolved a_freq / b_freq offsets, then vibrato from the LFO and a few cents of
// drift. The mod envelope has no destination in pitch yet, it moves timbre -
// that is provisional, and M4 replaces it with the documented bipolar
// a_mod / b_mod pitch modulation (decision R2, see alignment plan 6.1).
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
    float ampLevel() const { return env.ampValue(); }

private:
    float whiteNoise();
    void updateControlTargets (float modEnv, float lfoPitch, float lfoFilter,
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

    // The resolved cutoff pair is kept locked together by this octave offset
    // (flt_sep), which VoiceParams measured once at note on.
    float cutoffSepOct = 0.0f;

    VoiceParams p;
    int note = 60;
    bool active = false;
    std::int64_t startedAt = 0;
    std::int64_t sampleCount = 0;
    float peak = 0.0f;

    MorphOsc oscA, oscB;
    float subPhase = 0.0f, subInc = 0.0f;

    SharedEnv env;          // one clock: amp and modulation (design 6.1.1)
    Modulator lfo;
    Drift driftPitch, driftFilter;
    Rng rng { 0 };

    TrapezoidSVF filter1, filter2;
    DcBlocker dcBlock;

    float baseFreqA = 220.0f, baseFreqB = 220.0f, baseFreqSub = 110.0f;
    float detuneA = 0.0f, detuneB = 0.0f;
    float panGainL = 0.7f, panGainR = 0.7f;
    float noiseState = 0.0f;
};

} // namespace gmosh
