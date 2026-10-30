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
// its optional sample and hold (mod_sh), the one LFO (vibrato + tremolo),
// sample and hold on B (b_sh), and two slow drift generators, all per voice,
// plus per voice random detune and phase start.
//
// What this class does *not* do is arithmetic on genes. Every value it reads
// comes from VoiceParams, which is the one mapping layer (design doc 3, 5.7.2):
// the pitches, both cutoffs, the mix in equal power mix space, the gated levels,
// the semitone depths of a_mod / b_mod, the two S&H rates, the LFO's depth,
// polarity and onset delay. Re-deriving any of it here is how links end up
// applied twice and disagree.
//
// Pitch policy (design doc 5.3 + 6.2, decision R2): the played pitch is the note
// plus the resolved a_freq / b_freq offsets; on top of that sit the mod
// envelope's bounded pitch modulation (a_mod / b_mod, semitones, ceiling in
// VoiceParams) and the LFO's vibrato, plus a few cents of drift. That is what
// the reference documents, and Atonality at generation time is what keeps it
// small (plan 6.1).
//
// One detail of 6.4 lives in the render loop rather than here: everything is
// smoothed between control updates *except* a sample and hold's step, which is
// applied with no ramp, because the step is the sound.
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
    void updateControlTargets (float modEnv, float lfoVibSemitones,
                              float driftP, float driftF);
    /// 6.4's exception: an S&H step is applied at the sample it happens, with no
    /// ramp, because the step is the sound. updateControlTargets fills the *To
    /// values; this moves the current ones to match and zeroes the slopes.
    void applyTargetsImmediately ();

    // Control rate: modulation targets are evaluated every kCtrlPeriod samples
    // and ramped between (design doc 6.4). An S&H step is the exception and is
    // applied where it happens (see render).
    enum { kCtrlPeriod = 16 };
    int ctrlCountdown = 0;
    float freqA = 0.0f, freqATo = 0.0f, freqASlope = 0.0f;
    float freqB = 0.0f, freqBTo = 0.0f, freqBSlope = 0.0f;
    float colourA = 0.0f, colourATo = 0.0f, colourASlope = 0.0f;
    float colourB = 0.0f, colourBTo = 0.0f, colourBSlope = 0.0f;
    float cutoff1 = 0.0f, cutoff1To = 0.0f, cutoff1Slope = 0.0f;
    float cutoff2 = 0.0f, cutoff2To = 0.0f, cutoff2Slope = 0.0f;
    float fmIndex = 0.0f, fmIndexTo = 0.0f, fmIndexSlope = 0.0f;
    // The A/B mix lives here too, because mix_mod is a modulation destination
    // (6.2) and it has to share the equal power law with osc_mix rather than
    // have a second one next to it.
    float mixA = 0.7f, mixATo = 0.7f, mixASlope = 0.0f;
    float mixB = 0.7f, mixBTo = 0.7f, mixBSlope = 0.0f;
    /// The vibrato the last control update asked for, kept so that an S&H step
    /// between two control updates can re-target without re-deriving it.
    float vibSemitones = 0.0f;

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
    SampleHold modHold;     // mod_sh: re-quantises the mod value itself (6.1.1)
    SampleHold bHold;       // b_sh: holds oscillator B (4.2)
    Modulator lfo;          // one rate, two destinations: pitch and level (6.2)
    float tremoloDepth = 0.0f;   // lfo_bal's tremolo half x onset delay gate
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
