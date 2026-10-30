#pragma once

#include "Dsp.h"

#include "../Genetic/GeneTable.h"
#include "../Genetic/Mutation.h"

namespace gmosh
//==============================================================================
// Genome -> real world voice settings: the one mapping layer (M1).
//
// Design doc section 3: this happens at note on, never per sample. The genome
// stays normalized; all the ranges and curves live in the gene table.
//
// This is the only place a gene is turned into a number the DSP uses, which is
// what makes it the only place the six curation mechanisms of 5.7 can be
// applied once instead of at every destination:
//
//   5.7.1 neutral   - a gene at gi.neutral produces the identity value here
//                     (0 offset, no modulation, silence where there was silence);
//                     Tests/EngineTests.cpp, testMappingNeutralIsRest.
//   5.7.2 linking   - a_freq is the patch pitch and B's offset, both filter
//                     cutoffs and freqAHz/freqBHz follow it. Effect scales the
//                     wet path, VelSens scales mod_vel. Nothing downstream
//                     re-derives any of it.
//   5.7.3 gates     - vol_sus, vol_fade, mod_vel, lfo_dly, rvb_atk and mod_sh
//                     are read through gated()/gateOpen().
//   5.7.4 rails     - flt_kf is read through softRails().
//   5.7.5 steps     - a_freq and flt_type go through Curve::Stepped, so the
//                     only values that reach the voice are grid points.
//   5.7.7 curves    - osc_mix is warped into mix space before the equal power
//                     pan law, and the four curvature genes become shape
//                     exponents (M3), never seconds.
//
// Since M4 this is also the only place the modulation *budget* is decided: the
// mod envelope's five destinations (a_mod/b_mod in semitones, fm_mod, mix_mod,
// flt_mod), the two sample and hold rates (mod_sh on the mod value itself,
// b_sh on oscillator B), and the one LFO's depth, polarity, pitch/tremolo split
// and onset delay. Design 6.2's destination list and the voice's render loop
// have to agree; they do, because the voice reads numbers, never genes.
//
// The shared envelope of design 6.1.1 (Rev 4 / M4b) is resolved here as an
// EnvShape: a pluck repeated at env_loop, under an outer volume decay whose
// length is env_time x env_kf. env_time >= 0.9 simply means "no outer decay";
// the retired R3 special case is gone because the note's length and the curve's
// shape are no longer the same number. The voice owns no envelope arithmetic of
// its own, it runs the clock this struct describes.
//
// Gates, rails and warps are *not* applied to genomes: the genome is the truth
// the player sees and grows, this is how it is read. Voice reads resolved values
// only (5.7.2's "not ad hoc in the DSP").
//==============================================================================
{
/// Non-gene engine behaviour, named instead of mutated (design deviations D-3
/// and D-5). The reference documents these as behaviour, not controls, so they
/// are constants here rather than genes; M10 may revisit the amounts with
/// measurements from the harness.
inline constexpr float kNoteOffReleaseSeconds = 0.18f; ///< D-5: fixed note off release
inline constexpr float kVoiceDetuneCents     = 12.0f;  ///< D-3: per voice spread of the detune
inline constexpr float kVoiceDriftAmount     = 0.35f;  ///< D-3: per voice random walk depth

/// env_tilt -> where a pluck peaks within its period (design 6.1.1 Rev 4, plan
/// M4b gate 2). This is the single most character-defining pair of numbers in
/// the instrument, so it is named here rather than buried in the mapping layer:
/// the neutral gene must be the reference's plain pluck (5.7.1: neutral is
/// inaudible, and "a pluck" is what the instrument does at rest), and only the
/// top of the gene grows the slow rise that falls off a cliff.
inline constexpr float kPluckPeakFast    = 0.010f; ///< env_tilt = -1: as snappy as it gets
inline constexpr float kPluckPeakNeutral = 0.060f; ///< env_tilt =  0: a regular pluck
inline constexpr float kPluckPeakSwell   = 0.950f; ///< env_tilt = +1: rise, then a cliff

/// How far mod_atk may move the modulation pluck's peak away from the level's,
/// as a fraction of the period. mod_atk has no curvature meaning (D-8).
inline constexpr float kModPeakOffsetRange = 0.40f;

/// The ceiling on a_mod / b_mod (design doc 4.2 "Bipolar -> up to +-12 st",
/// decision R2). The gene endpoints are *full* swing - 0.0 is maximum negative,
/// 1.0 maximum positive, with no dead zone around the middle - so this constant
/// is the only thing that bounds what a held note can do to its own pitch.
/// The other half of the bound is generation time: a_mod / b_mod / lfo_amt are
/// kFlagPitch, so applyAtonality() pulls them to their neutral value as
/// Atonality closes and a plant with Atonality 0 cannot move pitch at all.
/// Tests/EngineTests.cpp (testPitchModulation) holds both halves.
inline constexpr float kPitchModSemitoneCeiling = 12.0f;

/// Host automatable macros (design doc 4.1). These are *not* genes: they never
/// mutate, they scale what the player is already holding.
struct MacroValues
{
    float volume = 0.8f;
    float velSens = 0.6f;
    float tuningCents = 0.0f;
    float effect = 0.6f;
    float wheelScale = 0.7f;
    float wheel = 0.0f;
};

struct VoiceParams
{
    // Envelope clock (4.2 group 1). Since M4b this is a pluck under a volume
    // decay: envDuration is how long the whole train of plucks takes to fade
    // (EnvShape::kInfiniteDuration = no decay at all, design 6.1.1), and
    // envPeriodSeconds is one pluck - env_loop when the note repeats, the whole
    // note when it does not. envPeakPosition is env_tilt (where the pluck
    // peaks), and nothing here is a duration the shape has to fit into.
    float envDuration = 1.0f, envLoopSeconds = 0.0f, envTilt = 0.0f;
    bool  envRepeats = false;    ///< env_loop shorter than the note (4.2)
    float envPeriodSeconds = 1.0f; ///< one pluck, always finite
    float envPeakPosition = 0.06f; ///< env_tilt -> peak position within the period
    float envKeyFollow = 0.0f;   ///< resolved octaves/key of time scaling, for M8

    // The pluck and the decay under it. vol_atk is the curvature of the rise
    // into the peak, vol_dcy the curvature of the *outer* volume decay over the
    // repeats (deviation D-8: it has no say in the pluck's own fall). Both are
    // exponents, 1.0 = linear, never seconds (5.7.7).
    float pluckRiseExp = 1.0f, outerDecayExp = 1.0f;
    // The modulation envelope has no shape of its own (design 6.1.1, Rev 4):
    // mod_atk offsets its peak within the same period, mod_dcy scales its decay
    // time relative to the level's. Both are silent at their neutral value.
    float modPeakOffset = 0.0f, modTimeScale = 1.0f;

    // Levels, read through their gates (5.7.3). vol_sus is the floor of the
    // outer curve, vol_fade pulls that floor to silence.
    float ampSustain = 0.0f;     ///< max(0, 2(v-0.5)): below the gate, no sustain
    float volFade = 0.0f;        ///< pulls the outer floor to silence, 0..1
    /// The one floor the clock is given: vol_sus with vol_fade already folded in
    /// for a note that has a length, and (1 - vol_fade) or 1.0 for one that has
    /// none. 1.0 means "this note cannot end by itself" (design 6.1.1 Rev 4).
    float envOuterFloor = 0.0f;
    float ampRelease = kNoteOffReleaseSeconds;   ///< D-5: fixed note off release

    // Modulation envelope depth: VelSens x mod_vel, applied *once*, to the mod
    // envelope itself (design 6.1.1). The destination amounts stay raw and
    // bipolar, because scaling both the envelope and its destinations is how a
    // velocity curve quietly ends up squared.
    float modVelocity = 0.0f, modScale = 1.0f;

    // mod_sh's documented destination is a sample and hold *rate* on the mod
    // envelope, gated to the top quarter and inverted (off below the gate). It
    // is resolved here so the mapping layer is complete, and re-quantises the
    // mod value the voice hands to every mod envelope destination (M4).
    float modSampleHoldHz = 0.0f;

    // lfo (4.2 group 1). One rate, two destinations: pitch and tremolo, split
    // at equal power by lfo_bal. lfo_amt is bipolar and its sign is a *second*
    // piece of information: above its neutral A and B move together, below it
    // they move in opposite directions. The voice is what makes that sign
    // audible (it applies opposite vibrato to A and B), so both halves are
    // resolved here rather than being re-derived from the gene downstream.
    float lfoRate = 1.0f, lfoDepth = 0.0f, lfoBalance = 0.5f, lfoDelaySeconds = 0.0f;
    float lfoSignB = 1.0f;           ///< +1 both the same, -1 opposite phase on B

    // oscillators
    float aForm = 0.0f, aColour = 0.5f, aNoise = 0.0f, aRatio = 0.0f;
    float bForm = 0.0f, bColour = 0.5f, bNoise = 0.0f, bRatio = 0.0f;

    // a_mod / b_mod: the mod envelope's bipolar amount *in semitones*, at the
    // top of the envelope. R2 verbatim: "0.5 means no pitch modulation. 0.0 is
    // the maximum negative, and 1.0 is the maximum positive" - identically on A
    // and B, no dead zone, bounded by kPitchModSemitoneCeiling. The voice
    // multiplies by the (velocity scaled, possibly S&H'd) mod value.
    float aModSemis = 0.0f, bModSemis = 0.0f;

    // b_sh: a real sample and hold rate on B (4.2). Below the middle of the
    // gene the rate is a division of B's own frequency "for a consistent timbre
    // across pitches"; at and above it, a free 0.5..16 Hz. bSampleHoldMix is
    // the crossfade in from the neutral value (see resolveVoiceParams).
    float bSampleHoldHz = 0.0f, bSampleHoldMix = 0.0f;

    // mix / cross modulation. balance is in *mix space*, not gene space: the
    // osc_mix warp of 5.7.7 is already applied, so 0.5 is equal power and 0 / 1
    // are A alone / B alone. mix_mod (M4) adds to this value rather than to the
    // gene, which is the only way the two can share one equal power curve.
    float balance = 0.5f, fmAmount = 0.0f, fmMod = 0.0f, mixMod = 0.0f, subLevel = 0.0f;

    // filter. filterHz and cutoff2Hz are the resolved tracking pair: both are
    // ratios of A's pitch (5.7.2) and both are computed here, once per note on.
    // filterSep is kept alongside them because the voice has to move the two
    // cutoffs together while modulation deviates them.
    float filterHz = 4000.0f, cutoff2Hz = 4000.0f, filterQ = 0.5f,
          keyFollow = 0.0f, filterMod = 0.0f, filterSep = 0.0f;
    /// flt_type as a stepped value in [0,1] (0 bandpass, .33 serial LP, .66
    /// parallel LP, 1 notch). Quantised by the table's Curve::Stepped; the
    /// audible topology stays the trait until M5 (decision R10).
    float filterTopology = 0.6667f;

    // saturation
    float drive = 0.0f;

    // effects (per voice wet path, M7)
    float fxSize = 0.6f, fxLength = 1.5f, fxDamp = 0.5f, fxChorus = 0.0f, fxMix = 0.0f, fxWidth = 0.5f;
    float reverbBloomSeconds = 0.0f;

    // output stage (M7)
    float bassDb = 0.0f, trebleDb = 0.0f, panSpread = 0.0f, clipDrive = 0.0f;

    // per voice "life" (not genes any more, D-3)
    float driftAmount = kVoiceDriftAmount, detuneCents = kVoiceDetuneCents;

    // Resolved links (5.7.2): the pitch the played note maps to after a_freq,
    // in Hz and before any Nyquist clamping. Voice reads these instead of doing
    // the arithmetic again, and M11's "grown vs base" display reads them too.
    float freqAHz = 440.0f, freqBHz = 440.0f;

    // derived
    float gain = 1.0f;
    float velocity = 1.0f;
    float tuneCents = 0.0f;

    unsigned char fmDirection = 0;
    unsigned char oscBMode = 0;

    /// The one place the resolved envelope becomes the clock's configuration.
    /// The voice calls this at note on and then runs the clock; it never
    /// interprets a gene itself (design 3, 5.7.2).
    EnvShape envShape() const
    {
        EnvShape s;
        s.noteSeconds    = envDuration;
        s.periodSeconds  = envPeriodSeconds;
        s.repeats        = envRepeats;
        s.peak           = clampf (envPeakPosition, 1.0e-3f, 0.999f);
        s.riseExp        = pluckRiseExp;
        s.outerExp       = outerDecayExp;
        s.outerFloor     = clamp01 (envOuterFloor);
        s.modPeak        = clampf (s.peak + modPeakOffset, 1.0e-3f, 0.999f);
        s.modTimeScale   = modTimeScale;
        s.releaseSeconds = ampRelease;
        return s;
    }
};

/// Resolve a (already distance displaced) genome into playable settings.
void resolveVoiceParams (const Genome& genome, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& out);

} // namespace gmosh
