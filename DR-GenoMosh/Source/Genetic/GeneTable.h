#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gmosh
{
//==============================================================================
// The gene table.
//
// Design doc 4.3: the genome is a plain float array and *all* knowledge about
// what a gene means (its curve, its real world range, how strongly it mutates,
// where the musically useful part of its range is) lives in this data table.
// The mutation code and the UI are both driven by it, so tuning the synth means
// editing the table below rather than hunting through the engine.
//
// GENO_MOSH_GENE_LIST is the single source of truth: it generates both the
// GeneId enum and the kGenes table, so they can never drift apart.
//
//   id        UI label        curve      min        max       group      sens  softLo softHi flags
//
// 'sens' is the starting mutation sensitivity (0 = never moves, 1 = free).
// 'softLo'/'softHi' are the preferred region of [0,1] for that gene: random
// seeds are drawn from inside it and branch displacement is compressed outside
// it (see Curves.h / softCompress), which is what keeps random results usable.
//
// kFlagPitch marks the genes that can move the played pitch (oscillator
// offsets, vibrato, drift, per voice detune). Atonality is a ceiling on those
// genes at generation time, see Mutation.h / applyAtonality.
//
// Everything here is an [inferred] guess from the reference product's parameter
// names, exactly as flagged in the design document. Tune by ear.
#define GENO_MOSH_GENE_LIST(X) \
    /* --- amplitude envelope ------------------------------------------------ */ \
    X(vol_atk,   "Amp Attack",   Curve::Exp,     0.001f,   4.0f,  GrpAmp,   0.50f, 0.02f, 0.75f, 0) \
    X(vol_dcy,   "Amp Decay",    Curve::Exp,     0.010f,   8.0f,  GrpAmp,   0.50f, 0.05f, 0.75f, 0) \
    X(vol_sus,   "Amp Sustain",  Curve::Linear,  0.0f,     1.0f,  GrpAmp,   0.40f, 0.15f, 0.90f, 0) \
    X(vol_rel,   "Amp Release",  Curve::Exp,     0.020f,  12.0f,  GrpAmp,   0.50f, 0.05f, 0.65f, 0) \
    /* --- modulation envelope ----------------------------------------------- */ \
    X(mod_atk,   "Mod Attack",   Curve::Exp,     0.001f,   4.0f,  GrpMod,   0.50f, 0.02f, 0.70f, 0) \
    X(mod_dcy,   "Mod Decay",    Curve::Exp,     0.010f,  12.0f,  GrpMod,   0.50f, 0.05f, 0.70f, 0) \
    X(mod_sh,    "Mod Shape",    Curve::Linear,  0.0f,     1.0f,  GrpMod,   0.40f, 0.10f, 0.90f, 0) \
    X(mod_vel,   "Mod Velocity", Curve::Linear,  0.0f,     1.0f,  GrpMod,   0.30f, 0.10f, 0.90f, 0) \
    /* --- looping / tilting envelope ---------------------------------------- */ \
    X(env_time,  "Env Time",     Curve::Exp,     0.050f,  16.0f,  GrpLoop,  0.50f, 0.10f, 0.70f, 0) \
    X(env_loop,  "Env Loop",     Curve::Linear,  0.0f,     1.0f,  GrpLoop,  0.30f, 0.00f, 1.00f, 0) \
    X(env_tilt,  "Env Tilt",     Curve::Bipolar,-1.0f,     1.0f,  GrpLoop,  0.40f, 0.20f, 0.80f, 0) \
    /* --- LFO --------------------------------------------------------------- */ \
    X(lfo_rate,  "LFO Rate",     Curve::Exp,     0.020f,  30.0f,  GrpLfo,   0.60f, 0.05f, 0.60f, 0) \
    X(lfo_amt,   "LFO Amount",   Curve::Squared, 0.0f,     1.0f,  GrpLfo,   0.50f, 0.10f, 0.80f, kFlagPitch) \
    X(lfo_bal,   "LFO Balance",  Curve::Bipolar,-1.0f,     1.0f,  GrpLfo,   0.50f, 0.20f, 0.80f, 0) \
    /* --- oscillator A ------------------------------------------------------ */ \
    X(a_form,    "A Waveform",   Curve::Linear,  0.0f,     1.0f,  GrpOscA,  0.50f, 0.05f, 0.95f, 0) \
    X(a_color,   "A Colour",     Curve::Linear,  0.0f,     1.0f,  GrpOscA,  0.50f, 0.10f, 0.90f, 0) \
    X(a_noise,   "A Noise",      Curve::Squared, 0.0f,     1.0f,  GrpOscA,  0.40f, 0.00f, 0.50f, 0) \
    X(a_freq,    "A Frequency",  Curve::Bipolar,-24.0f,   24.0f,  GrpOscA,  0.40f, 0.20f, 0.80f, kFlagRatio | kFlagPitch) \
    X(a_mod,     "A Mod Amount", Curve::Bipolar,-1.0f,     1.0f,  GrpOscA,  0.50f, 0.15f, 0.85f, 0) \
    /* --- oscillator B ------------------------------------------------------ */ \
    X(b_form,    "B Waveform",   Curve::Linear,  0.0f,     1.0f,  GrpOscB,  0.50f, 0.05f, 0.95f, 0) \
    X(b_color,   "B Colour",     Curve::Linear,  0.0f,     1.0f,  GrpOscB,  0.50f, 0.10f, 0.90f, 0) \
    X(b_noise,   "B Noise",      Curve::Squared, 0.0f,     1.0f,  GrpOscB,  0.40f, 0.00f, 0.50f, 0) \
    X(b_freq,    "B Frequency",  Curve::Bipolar,-24.0f,   24.0f,  GrpOscB,  0.40f, 0.20f, 0.80f, kFlagRatio | kFlagPitch) \
    X(b_mod,     "B Mod Amount", Curve::Bipolar,-1.0f,     1.0f,  GrpOscB,  0.50f, 0.15f, 0.85f, 0) \
    X(b_sh,      "B Shape",      Curve::Linear,  0.0f,     1.0f,  GrpOscB,  0.40f, 0.00f, 0.70f, 0) \
    /* --- mix and cross modulation ------------------------------------------- */ \
    X(balance,   "Balance",      Curve::Linear,  0.0f,     1.0f,  GrpMix,   0.40f, 0.15f, 0.85f, 0) \
    X(fm_amt,    "FM Amount",    Curve::Squared, 0.0f,     1.0f,  GrpMix,   0.50f, 0.00f, 0.60f, 0) \
    X(fm_mod,    "FM Mod",       Curve::Bipolar,-1.0f,     1.0f,  GrpMix,   0.50f, 0.15f, 0.85f, 0) \
    X(sub_am,    "Sub / AM",     Curve::Linear,  0.0f,     1.0f,  GrpMix,   0.30f, 0.00f, 0.60f, 0) \
    X(det_spread,"Detune",       Curve::Linear,  0.0f,     1.0f,  GrpMix,   0.40f, 0.00f, 0.50f, kFlagPitch) \
    /* --- filter ------------------------------------------------------------- */ \
    X(flt_freq,  "Cutoff",       Curve::Exp,    40.0f, 18000.0f,  GrpFilter,0.60f, 0.15f, 0.85f, 0) \
    X(flt_q,     "Resonance",    Curve::Squared,0.0f,     1.0f,  GrpFilter,0.40f, 0.00f, 0.60f, 0) \
    X(flt_kf,    "Key Follow",   Curve::Bipolar,-1.0f,     1.0f,  GrpFilter,0.30f, 0.20f, 0.80f, 0) \
    X(flt_mod,   "Filter Mod",   Curve::Bipolar,-1.0f,     1.0f,  GrpFilter,0.50f, 0.15f, 0.85f, 0) \
    X(flt_sep,   "Filter Spread",Curve::Linear,  0.0f,     1.0f,  GrpFilter,0.40f, 0.00f, 0.50f, 0) \
    /* --- effects (chorus + reverb) ------------------------------------------- */ \
    X(fx_size,   "FX Size",      Curve::Linear,  0.0f,     1.0f,  GrpFx,    0.40f, 0.10f, 0.90f, 0) \
    X(fx_len,    "FX Length",    Curve::Exp,     0.050f,  12.0f,  GrpFx,    0.40f, 0.05f, 0.60f, 0) \
    X(fx_damp,   "FX Damping",   Curve::Linear,  0.0f,     1.0f,  GrpFx,    0.40f, 0.10f, 0.80f, 0) \
    X(fx_chors,  "FX Chorus",    Curve::Linear,  0.0f,     1.0f,  GrpFx,    0.40f, 0.00f, 0.60f, 0) \
    X(fx_mix,    "FX Mix",       Curve::Linear,  0.0f,     1.0f,  GrpFx,    0.30f, 0.00f, 0.70f, 0) \
    X(fx_width,  "FX Width",     Curve::Linear,  0.0f,     1.0f,  GrpFx,    0.20f, 0.20f, 0.90f, 0) \
    /* --- per voice "life" ----------------------------------------------------- */ \
    X(drift_amt, "Drift",        Curve::Linear,  0.0f,     1.0f,  GrpMeta,  0.40f, 0.10f, 0.70f, kFlagPitch) \
    X(sat_drive, "Drive",        Curve::Squared, 0.0f,     1.0f,  GrpMeta,  0.40f, 0.00f, 0.50f, 0)

//==============================================================================
enum Curve : unsigned char
{
    Linear,   ///< value = min + v * (max - min)
    Exp,      ///< value = min * (max/min)^v, requires min > 0 (times, Hz)
    Squared,  ///< value = min + v^2 * (max - min), keeps small values subtle
    Cubic,
    Bipolar,  ///< v = 0.5 is zero, range is [min, max] with min < 0 < max
};

enum Group : unsigned char
{
    GrpAmp, GrpMod, GrpLoop, GrpLfo, GrpOscA, GrpOscB, GrpMix, GrpFilter, GrpFx, GrpMeta,
    GrpCount
};

enum GeneFlags : unsigned int
{
    kFlagNone   = 0,
    kFlagNever  = 1u << 0, ///< never mutated (macros live outside the genome)
    kFlagRatio  = 1u << 1, ///< pitch offset, goes through the ratio snapper (5.5)
    kFlagPitch  = 1u << 2, ///< can move the played pitch: bounded by Atonality
};

struct GeneInfo
{
    const char* id;
    const char* label;
    Curve curve;
    float minValue, maxValue;
    Group group;
    float sensitivity;
    float softMin, softMax;
    unsigned int flags;
};

// std::size_t underlying type on purpose: gene ids are used to index the
// genome array, and the project builds with -Wsign-conversion.
enum GeneId : std::size_t
{
#define GMO_GENE_ENUM(id, ...) g_##id,
    GENO_MOSH_GENE_LIST (GMO_GENE_ENUM)
#undef GMO_GENE_ENUM
    kNumGenes
};

/// The genome: every gene, normalized to [0,1]. Design doc section 3.
using Genome = std::array<float, kNumGenes>;

extern const GeneInfo kGenes[kNumGenes];
extern const char* const kGroupNames[GrpCount];

/// Group index for a gene id (kept in the table, this is just a typed getter).
inline Group geneGroup (std::size_t gene) { return kGenes[gene].group; }

//==============================================================================
// Plant layout.
enum
{
    kNumBranches = 12,        ///< one branch per pitch class [inferred]
    kNumDirections = 3,       ///< guard rail retries (5.6 "silence rejection")
};

} // namespace gmosh
