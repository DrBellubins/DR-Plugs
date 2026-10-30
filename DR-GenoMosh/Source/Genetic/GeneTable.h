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
// where the musically useful part of its range is, what its "nothing happens"
// value is) lives in this data table. The mutation code and the UI are both
// driven by it, so tuning the synth means editing the table below rather than
// hunting through the engine.
//
// GENO_MOSH_GENE_LIST is the single source of truth: it generates both the
// GeneId enum and the kGenes table, so they can never drift apart. The test
// suite (Tests/EngineTests.cpp, testTableMatchesDoc) holds the first 48 rows
// against design doc 4.2 by id, order, group, curve and step count.
//
// The list is design doc 4.2 verbatim: 48 documented genes in the reference's
// own grouping and order (16 / 15 / 17), then the two gene extras block of
// deviation D-7 in its own group (decision R1). Do not interleave them; the
// DAW parameter list and the UI pages are built from this order.
//
//   id  label  helpText  curve  min  max  neutral  gateLow gateHigh  group
//   steps  sens  softLo softHi  flags
//
// 'sens' is the starting mutation sensitivity (0 = never moves, 1 = free).
// 'softLo'/'softHi' are the preferred region of [0,1] for that gene: random
// seeds are drawn around neutral and clamped to it, and branch displacement is
// compressed outside it (see Curves.h / softCompress), which is what keeps
// random results usable.
//
// 'neutral' is the value at which the gene does nothing audible (5.7.1);
// 'gateLow'/'gateHigh' are its activation dead zone (5.7.3) and 0,1 means "no
// gate". Both are read by the mapping layer and never twice: VoiceParams applies
// gated()/gateOpen()/softRails() as it turns a gene into a DSP value (M1), and
// a destination that gates something a second time is how a dead zone quietly
// becomes "half on". The lower rail belongs to the dead zone (for every gated
// gene gateLow *is* the neutral value), the upper rail belongs to the gene.
//
// kFlagPitch marks the genes that can move the played pitch. Atonality is a
// ceiling on those genes at generation time, see Mutation.h / applyAtonality.
// kFlagLinked marks the genes whose value is the reference for another gene
// (a_freq -> B and both cutoffs, flt_freq -> the tracking cutoff); links are
// resolved once, in VoiceParams (5.7.2). kFlagStructural marks a gene whose
// value selects a topology rather than a level, so crossing its steps has to be
// crossfaded, never slewed (5.4). kFlagNever makes a gene growth-exempt (5.7.6).
//
// Gene semantics follow the design document (4.2). The curves, ranges,
// sensitivities and soft ranges are our own choices within it. Tune by ear, and
// with the harness (Docs/Rev3-Alignment-Plan.md M10).
#define GENO_MOSH_GENE_LIST(X) \
    /* --- 4.2 group 1: envelope clock (4) + volume (4) + modulation (4) + LFO (4) = 16 --- \
       One shared clock drives both envelopes; the duration of the sound is a \
       gene, so growth changes articulation as much as spectrum. */ \
    X(env_time,  "Env Duration",  "Total duration of the sound; above 0.9 it is held until the key is released.", Curve::Exp,     0.05f,  30.0f, 0.50f, 0.00f, 1.00f, GrpEnv,   0, 0.45f, 0.12f, 0.72f, 0) \
    X(env_loop,  "Env Repeat",    "Period of the repeating envelope; a period longer than the duration means no repeats.", Curve::Exp, 0.05f, 30.0f, 0.50f, 0.00f, 1.00f, GrpEnv,  0, 0.35f, 0.15f, 0.75f, 0) \
    X(env_tilt,  "Env Tilt",      "Attack/decay skew of the shared envelope: low is a fast attack, high a slow one.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpEnv,  0, 0.40f, 0.25f, 0.75f, 0) \
    X(env_kf,    "Env Key Follow","Key follow of the duration; above the middle, high keys shorten the envelope.", Curve::Bipolar, -2.0f, 2.0f, 0.50f, 0.00f, 1.00f, GrpEnv,   0, 0.25f, 0.30f, 0.70f, 0) \
    X(vol_atk,   "Attack Shape",  "Attack curvature of the amp envelope: 0.5 is linear, below is front weighted, above back weighted.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpVol, 0, 0.35f, 0.25f, 0.75f, 0) \
    X(vol_dcy,   "Decay Shape",   "Decay curvature of the amp envelope: 0.5 is linear.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpVol, 0, 0.35f, 0.25f, 0.75f, 0) \
    X(vol_sus,   "Sustain",       "Sustain level; below the middle the note does not sustain at all.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.50f, 1.00f, GrpVol, 0, 0.40f, 0.35f, 0.90f, 0) \
    X(vol_fade,  "Fade",          "A second fade after the attack, only active above the top quarter of the range.", Curve::Linear, 0.0f, 1.0f, 0.75f, 0.75f, 1.00f, GrpVol, 0, 0.25f, 0.75f, 0.95f, 0) \
    X(mod_atk,   "Mod Atk Shape", "Attack curvature of the modulation envelope, which drives parameters, not level.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpMod, 0, 0.35f, 0.25f, 0.75f, 0) \
    X(mod_dcy,   "Mod Dec Shape", "Decay curvature of the modulation envelope: 0.5 is linear.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpMod, 0, 0.35f, 0.25f, 0.75f, 0) \
    X(mod_sh,    "Mod Sample Hold","Sample and hold rate for the modulation envelope; off until the top quarter, higher is slower.", Curve::Exp, 8.0f, 0.1f, 0.75f, 0.75f, 1.00f, GrpMod, 0, 0.30f, 0.10f, 0.90f, 0) \
    X(mod_vel,   "Mod Velocity",  "How much velocity scales modulation depth; below a tenth velocity is ignored.", Curve::Linear, 0.0f, 1.0f, 0.10f, 0.10f, 1.00f, GrpMod, 0, 0.30f, 0.10f, 0.90f, 0) \
    X(lfo_rate,  "LFO Rate",      "Frequency of the one LFO; vibrato and tremolo share it.", Curve::Exp,    0.02f,  30.0f, 0.50f, 0.00f, 1.00f, GrpLfo,   0, 0.60f, 0.05f, 0.60f, 0) \
    X(lfo_amt,   "LFO Amount",    "LFO depth; the middle is none, below it A and B move in opposite directions.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpLfo, 0, 0.50f, 0.25f, 0.85f, kFlagPitch) \
    X(lfo_bal,   "Vibrato/Tremolo","0 is vibrato only, 0.5 both at one rate, 1 is tremolo only.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpLfo, 0, 0.50f, 0.25f, 0.75f, 0) \
    /* lfo_dly's range is 4.2's "0 below 0.5, then 0 -> 8 s", which is what \ \
       M4 wired: seconds the LFO depth fades in over. */ \
    X(lfo_dly,   "LFO Delay",     "Delay before the LFO fades in; none below the middle.", Curve::Linear, 0.0f, 8.0f, 0.50f, 0.50f, 1.00f, GrpLfo, 0, 0.30f, 0.50f, 0.80f, 0) \
    /* --- 4.2 group 2: oscillator A (5) + FM/mix (4) + oscillator B (6) = 15 -- */ \
    X(a_form,    "A Waveform",    "Waveform morph for A: sine, then saw, then square, then pulse.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOscA, 0, 0.50f, 0.05f, 0.95f, 0) \
    X(a_noise,   "A Noise",       "How much noise is mixed into A; 1.0 buries it.", Curve::Squared, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpOscA, 0, 0.40f, 0.00f, 0.50f, 0) \
    X(a_mod,     "A Pitch Mod",   "Bipolar pitch modulation of A from the modulation envelope; the middle is none.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOscA, 0, 0.50f, 0.30f, 0.70f, kFlagPitch) \
    X(a_color,   "Noise Colour",  "Colour of the shared noise source: brown and muddy at one end, white and sharp at the other.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOscA, 0, 0.40f, 0.10f, 0.90f, 0) \
    X(a_freq,    "A Octave",      "Pitch of A on a seven step octave grid. It is the pitch of the patch; B and the filters follow it.", Curve::Stepped, -24.0f, 24.0f, 0.50f, 0.00f, 1.00f, GrpOscA, 7, 0.30f, 0.20f, 0.80f, kFlagPitch | kFlagLinked) \
    X(fm_mod,    "FM Envelope",   "How the modulation envelope changes the FM amount; the middle has no effect.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpMix, 0, 0.50f, 0.30f, 0.70f, 0) \
    X(fm_amt,    "FM Amount",     "Frequency modulation of A by B; the middle is none, the ends are the deepest.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpMix, 0, 0.50f, 0.30f, 0.70f, 0) \
    X(mix_mod,   "Mix Envelope",  "How the modulation envelope moves the A/B mix; the middle has no effect.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpMix, 0, 0.40f, 0.35f, 0.65f, 0) \
    X(osc_mix,   "A/B Mix",       "Mix of A and B. Equal power sits near 0.79, where the reference's factory state sits.", Curve::Linear, 0.0f, 1.0f, 0.7937f, 0.00f, 1.00f, GrpMix, 0, 0.40f, 0.50f, 0.92f, 0) \
    X(b_form,    "B Waveform",    "Waveform morph for B.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOscB, 0, 0.50f, 0.05f, 0.95f, 0) \
    X(b_noise,   "B Noise",       "How much noise is mixed into B.", Curve::Squared, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpOscB, 0, 0.40f, 0.00f, 0.50f, 0) \
    X(b_mod,     "B Pitch Mod",   "Bipolar pitch modulation of B from the modulation envelope; the middle is none.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOscB, 0, 0.50f, 0.30f, 0.70f, kFlagPitch) \
    X(sub_am,    "Sub AM",        "Sub oscillator: amplitude modulation of B one octave below, as a mix amount.", Curve::Linear, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpOscB, 0, 0.30f, 0.00f, 0.60f, 0) \
    X(b_freq,    "B Pitch",       "Pitch of B relative to A in semitones; the middle is unison.", Curve::Bipolar, -24.0f, 24.0f, 0.50f, 0.00f, 1.00f, GrpOscB, 0, 0.35f, 0.25f, 0.80f, kFlagPitch | kFlagRatio) \
    X(b_sh,      "B Sample Hold", "Sample and hold rate for B; below the middle the rate follows B's frequency.", Curve::Linear, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpOscB, 0, 0.35f, 0.00f, 0.70f, 0) \
    /* --- 4.2 group 3: filter (6) + saturation (1) + reverb (6) + output (4) = 17 --- \
       The five growth exempt genes (rvb_size, adj_*) are the reference's output \
       surface: they belong to the player, not to the plant (5.7.6). */ \
    X(flt_type,  "Filter Type",   "Filter topology: bandpass, dual serial lowpass, dual parallel lowpass, or notch.", Curve::Stepped, 0.0f, 1.0f, 0.6667f, 0.00f, 1.00f, GrpFilter, 4, 0.30f, 0.10f, 0.90f, kFlagStructural) \
    X(flt_q,     "Resonance",     "Filter resonance, capped by topology, cutoff and separation.", Curve::Squared, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpFilter, 0, 0.40f, 0.00f, 0.60f, 0) \
    X(flt_mod,   "Filter Envelope","Bipolar cutoff modulation from the modulation envelope; the middle is none.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpFilter, 0, 0.50f, 0.30f, 0.70f, 0) \
    X(flt_sep,   "Filter Spread", "Distance between the two cutoffs in octaves; the middle is no separation.", Curve::Bipolar, -3.0f, 3.0f, 0.50f, 0.00f, 1.00f, GrpFilter, 0, 0.40f, 0.30f, 0.70f, 0) \
    X(flt_freq,  "Cutoff",        "Cutoff relative to A's pitch, so a branch keeps its timbre at every octave.", Curve::Linear, -2.0f, 8.0f, 0.75f, 0.00f, 1.00f, GrpFilter, 0, 0.50f, 0.25f, 0.75f, kFlagLinked) \
    X(flt_kf,    "Cutoff Key Follow","Key follow of the cutoff, soft-railed: nothing below a quarter, exact above three quarters.", Curve::Linear, -1.0f, 1.0f, 0.50f, 0.25f, 0.75f, GrpFilter, 0, 0.30f, 0.30f, 0.70f, 0) \
    X(saturate,  "Saturate",      "Per voice warm distortion after the filter; off until the middle, then more drive.", Curve::Squared, 0.0f, 1.0f, 0.00f, 0.50f, 1.00f, GrpSat, 0, 0.40f, 0.00f, 0.50f, 0) \
    X(rvb_mix,   "Reverb Mix",    "Dry/wet of the built in chorusing reverb; the Effect macro scales it further.", Curve::Linear, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpRevb, 0, 0.30f, 0.00f, 0.70f, 0) \
    X(rvb_atk,   "Reverb Bloom",  "Envelope on the reverb amount; a bloom after the attack, above the top third.", Curve::Linear, 0.0f, 1.0f, 0.66f, 0.66f, 1.00f, GrpRevb, 0, 0.30f, 0.66f, 0.90f, 0) \
    X(rvb_len,   "Reverb Length", "RT60 decay time of the reverb tail.", Curve::Exp, 0.20f, 12.0f, 0.50f, 0.00f, 1.00f, GrpRevb, 0, 0.40f, 0.05f, 0.60f, 0) \
    X(rvb_damp,  "Reverb Damping","High frequency damping inside the reverb tail.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpRevb, 0, 0.40f, 0.10f, 0.80f, 0) \
    X(rvb_chor,  "Reverb Chorus", "Chorus and unison added to the reverb tail; depth and rate rise together.", Curve::Linear, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpRevb, 0, 0.40f, 0.00f, 0.60f, 0) \
    X(rvb_size,  "Reverb Size",   "Perceived room size. Never changes when a branch grows.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpRevb, 0, 0.00f, 0.40f, 0.60f, kFlagNever) \
    X(adj_bass,  "Bass Shelf",    "Low shelf, plus or minus 24 dB at 100 Hz. Output, not part of the plant.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOut, 0, 0.00f, 0.40f, 0.60f, kFlagNever) \
    X(adj_treb,  "Treble Shelf",  "High shelf, plus or minus 24 dB at 6 kHz. Output, not part of the plant.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOut, 0, 0.00f, 0.40f, 0.60f, kFlagNever) \
    X(adj_pan,   "Branch Pan",    "How far the branches are spread across the field; reduced by lowering Effect.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpOut, 0, 0.00f, 0.40f, 0.60f, kFlagNever) \
    X(adj_clip,  "Clipper",       "Master soft clipper after the master volume.", Curve::Linear, 0.0f, 1.0f, 0.00f, 0.00f, 1.00f, GrpOut, 0, 0.00f, 0.00f, 0.20f, kFlagNever) \
    /* --- Extras (2): GenoMosh additions, deviation D-7, decision R1 ---------- \
       Appended after the 48 and never interleaved with them. An extra owns a \
       destination no documented gene owns; both are ordinary genes for growth \
       purposes. The UI and the parameter list show them as "GenoMosh \
       additions". */ \
    X(b_color,   "B Noise Colour","B's colour offset from the shared noise shaper; the middle shares A's colour.", Curve::Bipolar, -1.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpExtras, 0, 0.30f, 0.30f, 0.70f, 0) \
    X(fx_width,  "Reverb Width",  "Mid/side width of each voice's wet reverb signal; the dry branch is unaffected.", Curve::Linear, 0.0f, 1.0f, 0.50f, 0.00f, 1.00f, GrpExtras, 0, 0.20f, 0.20f, 0.90f, 0)

//==============================================================================
enum Curve : unsigned char
{
    Linear,   ///< value = min + v * (max - min)
    Exp,      ///< value = min * (max/min)^v, requires min > 0 (times, Hz).
              ///  min > max is legal and gives a decreasing curve (mod_sh).
    Squared,  ///< value = min + v^2 * (max - min), keeps small values subtle
    Bipolar,  ///< v = 0.5 is zero, range is [min, max] with min < 0 < max
    Stepped,  ///< quantised to `steps` equally spaced values across [min, max]
              ///  (5.7.5): a_freq 7 octave steps, flt_type 4 topologies.
};

enum Group : unsigned char
{
    // Design doc 4.3's grouping: the eleven documented groups in the order the
    // reference pages them, then the D-7 extras group (R1).
    GrpEnv, GrpVol, GrpMod, GrpLfo,
    GrpOscA, GrpMix, GrpOscB,
    GrpFilter, GrpSat, GrpRevb, GrpOut,
    GrpExtras,
    GrpCount
};

enum GeneFlags : unsigned int
{
    kFlagNone       = 0,
    kFlagNever      = 1u << 0, ///< growth exempt (5.7.6): rvb_size and the four adj_*
    kFlagRatio      = 1u << 1, ///< pitch offset that may go through the ratio snapper (5.5)
    kFlagPitch      = 1u << 2, ///< can move the played pitch: bounded by Atonality
    kFlagLinked     = 1u << 3, ///< its value is the reference for another gene (5.7.2)
    kFlagStructural = 1u << 4, ///< selects a topology, so its steps are crossfaded (5.4)
};

struct GeneInfo
{
    const char* id;
    const char* label;
    const char* helpText;             ///< one sentence, shown on hover (4.3, 12)
    Curve curve;
    float minValue, maxValue;         ///< DSP side mapping only, never stored
    float neutral;                    ///< the value at which nothing happens (5.7.1)
    float gateLow, gateHigh;          ///< activation dead zone (5.7.3); 0,1 = none
    Group group;
    unsigned int steps;               ///< for Curve::Stepped: a_freq = 7, flt_type = 4
    float sensitivity;
    float softMin, softMax;
    unsigned int flags;
    unsigned int colour;              ///< group colour, ARGB (see groupColour)
};

/// One colour per group (4.3). Kept as a function so the generated table cannot
/// drift from it, and as plain ARGB so this header stays free of UI types.
constexpr unsigned int groupColour (Group g)
{
    switch (g)
    {
        case GrpEnv:    return 0xffd8a53au;
        case GrpVol:    return 0xffe07a4au;
        case GrpMod:    return 0xffc06ad8u;
        case GrpLfo:    return 0xff8a7ad8u;
        case GrpOscA:   return 0xff5ac86au;
        case GrpMix:    return 0xff4ac0a0u;
        case GrpOscB:   return 0xff6ab0e8u;
        case GrpFilter: return 0xff5a8ce8u;
        case GrpSat:    return 0xffe85a7au;
        case GrpRevb:   return 0xff7ad0d8u;
        case GrpOut:    return 0xffb8bdbfu;
        case GrpExtras: return 0xff9a8f6au;
        case GrpCount:  break;   // not a group, only the count of them
        default:        break;
    }
    return 0xff808080u;
}

// std::size_t underlying type on purpose: gene ids are used to index the
// genome array, and the project builds with -Wsign-conversion.
enum GeneId : std::size_t
{
#define GMO_GENE_ENUM(id, ...) g_##id,
    GENO_MOSH_GENE_LIST (GMO_GENE_ENUM)
#undef GMO_GENE_ENUM
    kNumGenes
};

/// The first 48 entries of the table are design doc 4.2 verbatim; anything at
/// or after this index is the D-7 extras block (R1). The parameter list and the
/// UI pages keep them in that order.
inline constexpr std::size_t kNumCoreGenes = 48;
static_assert (kNumCoreGenes + 2 == kNumGenes,
               "the gene list must be the 48 documented genes plus exactly two extras");

inline constexpr bool isDocumentedGene (std::size_t gene) { return gene < kNumCoreGenes; }

/// The genome: every gene, normalized to [0,1]. Design doc section 3.
using Genome = std::array<float, kNumGenes>;

extern const GeneInfo kGenes[kNumGenes];
extern const char* const kGroupNames[GrpCount];

/// Group index for a gene id (kept in the table, this is just a typed getter).
inline Group geneGroup (std::size_t gene) { return kGenes[gene].group; }

//==============================================================================
// Version axes (design doc 11). These are three separate things and the state
// file stores all three:
//
//   kFormatVersion    the shape of the XML (elements, branch records);
//   kModelVersion     the meaning of the gene table itself. Bumping this is
//                     what makes an old state unreadable rather than silently
//                     wrong. 2 = the 48 + 2 Rev 3 table (M0); 1 = the 43 gene
//                     Rev 1 table. Decision R11: a mismatch is refused with a
//                     warning, there is no fake migration.
//   kMutationVersion  the growth algorithm and guard rails (5.2, 5.6).
inline constexpr int kFormatVersion   = 1;
inline constexpr int kModelVersion    = 2;
inline constexpr int kMutationVersion = 1;

//==============================================================================
// Plant layout.
enum
{
    kNumBranches = 12,        ///< one branch per pitch class
    kNumDirections = 3,       ///< guard rail retries (5.6 "silence rejection")
};

} // namespace gmosh
