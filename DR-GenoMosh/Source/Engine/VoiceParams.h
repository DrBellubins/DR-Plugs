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
// The shared envelope of design 6.1.1 is resolved here as an EnvShape (M3):
// env_time is the duration, env_kf scales it by key, env_loop is the repeat
// period, and env_time >= 0.9 is "infinite" (decision R3). The voice owns no
// envelope arithmetic of its own, it runs the clock this struct describes.
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
    // Envelope clock (4.2 group 1). Since M3 this *is* the length of the note:
    // envDuration is env_time scaled by env_kf, or EnvShape::kInfiniteDuration
    // once env_time reaches 0.9 (decision R3). envLoopSeconds is env_loop; the
    // shape's finite time base is derived from the two by SharedEnv, which is
    // the only place that reading of "infinite" lives (R3).
    float envDuration = 1.0f, envLoopSeconds = 0.0f, envTilt = 0.0f;
    bool  envRepeats = false;    ///< env_loop shorter than the duration (4.2)
    float envShapeSeconds = 1.0f; ///< one pass of the shape, always finite (R3)
    float envKeyFollow = 0.0f;   ///< resolved octaves/key of time scaling, for M8

    // Shape curvatures. vol_atk / vol_dcy / mod_atk / mod_dcy are exponents of
    // the shared shape (4.2: "0.5 is linear"), so the neutral gene maps to 1.0
    // and nothing here is ever a number of seconds (5.7.7).
    float ampAttackExp = 1.0f, ampDecayExp = 1.0f;
    float modAttackExp = 1.0f, modDecayExp = 1.0f;

    // Levels, read through their gates (5.7.3).
    float ampSustain = 0.0f;     ///< max(0, 2(v-0.5)): below the gate, no sustain
    float volFade = 0.0f;        ///< the second fade after the attack, 0..1
    float ampRelease = kNoteOffReleaseSeconds;   ///< D-5: fixed note off release

    // Modulation envelope depth: VelSens x mod_vel, applied *once*, to the mod
    // envelope itself (design 6.1.1). The destination amounts stay raw and
    // bipolar, because scaling both the envelope and its destinations is how a
    // velocity curve quietly ends up squared.
    float modVelocity = 0.0f, modScale = 1.0f;

    // mod_sh's documented destination is a sample and hold *rate* on the mod
    // envelope, gated to the top quarter and inverted (off below the gate). It
    // is resolved here so the mapping layer is complete; the destination is M4.
    float modSampleHoldHz = 0.0f;

    // lfo
    float lfoRate = 1.0f, lfoAmount = 0.0f, lfoBalance = 0.5f, lfoDelaySeconds = 0.0f;

    // oscillators
    float aForm = 0.0f, aColour = 0.5f, aNoise = 0.0f, aRatio = 0.0f, aMod = 0.0f;
    float bForm = 0.0f, bColour = 0.5f, bNoise = 0.0f, bRatio = 0.0f, bMod = 0.0f, bShape = 0.0f;

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
        s.duration       = envDuration;
        s.repeats        = envRepeats;
        s.loopPeriod     = envLoopSeconds;
        s.shapeSeconds   = envShapeSeconds;
        s.tilt           = envTilt;
        s.attackExp      = ampAttackExp;
        s.decayExp       = ampDecayExp;
        s.modAttackExp   = modAttackExp;
        s.modDecayExp    = modDecayExp;
        s.sustain        = ampSustain;
        s.fade           = volFade;
        s.releaseSeconds = ampRelease;
        return s;
    }
};

/// Resolve a (already distance displaced) genome into playable settings.
void resolveVoiceParams (const Genome& genome, const BranchTraits& traits,
                         const MacroValues& macros, int midiNote, float velocity,
                         VoiceParams& out);

} // namespace gmosh
