// Offline harness for the GenoMosh core (design doc section 13).
//
//   * phase 1/2 acceptance: curve round trips, ratio snapping direction (5.5),
//     determinism, distance 0 returns the seed, deviation grows with distance,
//     guard rails hold;
//   * the M1 mapping layer (Docs/Rev3-Alignment-Plan.md M1): neutral is the
//     identity at every destination, the six documented activation gates are
//     real at both ends, flt_kf's soft rails, osc_mix's documented equal power
//     point, seeds drawn around neutral, growth's pull toward neutral, and no
//     unconditional ratio snap at generation time;
//   * a numeric check of the trapezoidal SVF against a reference biquad;
//   * the M2 protocol of section 13: the WAV writer and reader, the note
//     generator and its density, and every metric checked against a signal
//     whose answer is known - before anything measured from the engine is
//     believed. Then the audio: the M1 table reproduced as a boundary golden,
//     the 12 x 5 batch with level, spectral and articulation metrics, the
//     createTest/performTest round trip at five sample rates, and the soak.
//
// The rendering, measuring and golden machinery itself is in Tests/Render.h;
// this file is the assertions and the command line.
//
// Build: see Tests/CMakeLists.txt, or directly
//   g++ -std=c++17 -O2 Source/Genetic/*.cpp Source/Engine/*.cpp Tests/EngineTests.cpp Tests/Render.cpp -o /tmp/gmosh_tests -lm
// Run: that binary with no arguments is the suite; with createTest / performTest
//   / batch / soak / noiseBank it is the section 13 tool.

#include "Engine/Synth.h"
#include "Engine/VoiceParams.h"
#include "Genetic/Curves.h"
#include "Genetic/PlantEngine.h"
#include "Render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <cstring>
#include <unistd.h>
#include <limits>
#include <string>
#include <vector>

using namespace gmosh;

static int failures = 0;

static void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

//==============================================================================
// The table is the contract (design doc 4.2 + 4.3). This test spells the
// expected table out by hand, from the document, rather than reading it back out
// of kGenes: it exists to catch the table drifting away from the document.
struct ExpectedGene
{
    const char* id;
    Group group;
    Curve curve;
    unsigned int steps;
};

static void testTableMatchesDoc()
{
    std::printf ("gene table vs design doc 4.2\n");

    // 4.2 group 1 (16) = Envelope 4 + Volume 4 + Modulation 4 + LFO 4.
    static const ExpectedGene expected[] = {
        { "env_time",  GrpEnv,    Curve::Exp,     0 },
        { "env_loop",  GrpEnv,    Curve::Exp,     0 },
        { "env_tilt",  GrpEnv,    Curve::Bipolar, 0 },
        { "env_kf",    GrpEnv,    Curve::Bipolar, 0 },
        { "vol_atk",   GrpVol,    Curve::Bipolar, 0 },
        { "vol_dcy",   GrpVol,    Curve::Bipolar, 0 },
        { "vol_sus",   GrpVol,    Curve::Linear,  0 },
        { "vol_fade",  GrpVol,    Curve::Linear,  0 },
        { "mod_atk",   GrpMod,    Curve::Bipolar, 0 },
        { "mod_dcy",   GrpMod,    Curve::Bipolar, 0 },
        { "mod_sh",    GrpMod,    Curve::Exp,     0 },
        { "mod_vel",   GrpMod,    Curve::Linear,  0 },
        { "lfo_rate",  GrpLfo,    Curve::Exp,     0 },
        { "lfo_amt",   GrpLfo,    Curve::Bipolar, 0 },
        { "lfo_bal",   GrpLfo,    Curve::Linear,  0 },
        { "lfo_dly",   GrpLfo,    Curve::Linear,  0 },
        // 4.2 group 2 (15) = Oscillator A 5 + FM/mix 4 + Oscillator B 6.
        { "a_form",    GrpOscA,   Curve::Linear,  0 },
        { "a_noise",   GrpOscA,   Curve::Squared, 0 },
        { "a_mod",     GrpOscA,   Curve::Bipolar, 0 },
        { "a_color",   GrpOscA,   Curve::Linear,  0 },
        { "a_freq",    GrpOscA,   Curve::Stepped, 7 },
        { "fm_mod",    GrpMix,    Curve::Bipolar, 0 },
        { "fm_amt",    GrpMix,    Curve::Bipolar, 0 },
        { "mix_mod",   GrpMix,    Curve::Bipolar, 0 },
        { "osc_mix",   GrpMix,    Curve::Linear,  0 },
        { "b_form",    GrpOscB,   Curve::Linear,  0 },
        { "b_noise",   GrpOscB,   Curve::Squared, 0 },
        { "b_mod",     GrpOscB,   Curve::Bipolar, 0 },
        { "sub_am",    GrpOscB,   Curve::Linear,  0 },
        { "b_freq",    GrpOscB,   Curve::Bipolar, 0 },
        { "b_sh",      GrpOscB,   Curve::Linear,  0 },
        // 4.2 group 3 (17) = Filter 6 + saturation 1 + reverb 6 + output 4.
        { "flt_type",  GrpFilter, Curve::Stepped, 4 },
        { "flt_q",     GrpFilter, Curve::Squared, 0 },
        { "flt_mod",   GrpFilter, Curve::Bipolar, 0 },
        { "flt_sep",   GrpFilter, Curve::Bipolar, 0 },
        { "flt_freq",  GrpFilter, Curve::Linear,  0 },
        { "flt_kf",    GrpFilter, Curve::Linear,  0 },
        { "saturate",  GrpSat,    Curve::Squared, 0 },
        { "rvb_mix",   GrpRevb,   Curve::Linear,  0 },
        { "rvb_atk",   GrpRevb,   Curve::Linear,  0 },
        { "rvb_len",   GrpRevb,   Curve::Exp,     0 },
        { "rvb_damp",  GrpRevb,   Curve::Linear,  0 },
        { "rvb_chor",  GrpRevb,   Curve::Linear,  0 },
        { "rvb_size",  GrpRevb,   Curve::Linear,  0 },
        { "adj_bass",  GrpOut,    Curve::Bipolar, 0 },
        { "adj_treb",  GrpOut,    Curve::Bipolar, 0 },
        { "adj_pan",   GrpOut,    Curve::Bipolar, 0 },
        { "adj_clip",  GrpOut,    Curve::Linear,  0 },
    };

    const std::size_t expectedCount = sizeof (expected) / sizeof (expected[0]);

    check (expectedCount == kNumCoreGenes && kNumCoreGenes == 48,
           "the documented gene count is 48");

    int wrong = 0;
    for (std::size_t i = 0; i < kNumCoreGenes && i < expectedCount; ++i)
    {
        const GeneInfo& gi = kGenes[i];
        const ExpectedGene& ex = expected[i];

        if (std::string (gi.id) != ex.id
            || gi.group != ex.group
            || gi.curve != ex.curve
            || (ex.curve == Curve::Stepped ? gi.steps != ex.steps : gi.steps != 0))
        {
            std::printf ("       slot %2d: expected %s / group %d / curve %d / steps %u, "
                         "found %s / %d / %d / %u\n",
                         static_cast<int> (i), ex.id, static_cast<int> (ex.group),
                         static_cast<int> (ex.curve), ex.steps,
                         gi.id, static_cast<int> (gi.group),
                         static_cast<int> (gi.curve), gi.steps);
            ++wrong;
        }
    }
    check (wrong == 0, "the first 48 table entries are 4.2 verbatim, in order");

    // The D-7 extras block (decision R1): two genes, after the 48, in their own
    // group, and nothing documented hides in it.
    static const char* const extras[] = { "b_color", "fx_width" };
    bool extrasOk = kNumGenes == kNumCoreGenes + 2;
    for (std::size_t i = 0; i < kNumGenes && extrasOk; ++i)
    {
        const bool inExtras = kGenes[i].group == GrpExtras;
        const bool expectedExtra = i >= kNumCoreGenes;
        if (inExtras != expectedExtra)
            extrasOk = false;
    }
    for (std::size_t e = 0; e < sizeof (extras) / sizeof (extras[0]); ++e)
        if (std::string (kGenes[kNumCoreGenes + e].id) != extras[e])
            extrasOk = false;
    check (extrasOk, "exactly two marked extras sit after the 48 (D-7, decision R1)");

    // 4.3 metadata: every gene documented, 5.7.1's neutral convention, gates in
    // range, steps only on stepped curves.
    int metaBad = 0;
    int neverCount = 0;
    for (std::size_t i = 0; i < kNumGenes; ++i)
    {
        const GeneInfo& gi = kGenes[i];

        if (gi.id == nullptr || gi.id[0] == 0 || gi.label == nullptr || gi.label[0] == 0
            || gi.helpText == nullptr || gi.helpText[0] == 0)
            ++metaBad;

        if (! std::isfinite (gi.neutral) || gi.neutral < 0.0f || gi.neutral > 1.0f)
            ++metaBad;
        if (gi.gateLow < 0.0f || gi.gateHigh > 1.0f || gi.gateLow >= gi.gateHigh)
            ++metaBad;
        if (gi.softMin > gi.softMax)
            ++metaBad;
        // min == max would make the mapping a constant; an Exp curve with
        // min > max is legal and deliberate (mod_sh: higher gene = slower rate).
        if (gi.minValue >= gi.maxValue && gi.curve != Curve::Exp)
            ++metaBad;
        if (gi.curve == Curve::Exp && (gi.minValue <= 0.0f || gi.maxValue <= 0.0f))
            ++metaBad;
        if (gi.curve == Curve::Stepped ? gi.steps < 2 : gi.steps != 0)
            ++metaBad;

        // 5.7.1: the "nothing happens" value of a bipolar gene is its middle.
        if (gi.curve == Curve::Bipolar && std::abs (gi.neutral - 0.5f) > 1.0e-4f)
            ++metaBad;

        // The four documented non neutral defaults of 4.2 are also neutrals here:
        // the reference's factory state is the state where nothing is added.
        if (std::string (gi.id) == "osc_mix"  && std::abs (gi.neutral - 0.7937f) > 1.0e-3f) ++metaBad;
        if (std::string (gi.id) == "flt_type" && std::abs (gi.neutral - 0.6667f) > 1.0e-3f) ++metaBad;
        if (std::string (gi.id) == "flt_freq" && std::abs (gi.neutral - 0.7500f) > 1.0e-3f) ++metaBad;

        if ((gi.flags & kFlagNever) != 0)
            ++neverCount;
    }
    check (metaBad == 0, "every gene carries help text, a neutral and documented gates");
    check (neverCount == 5, "rvb_size and the four adj_* are growth exempt (5.7.6)");

    // 5.5: Atonality bounds the pitch capable genes. After R7 that is exactly
    // the two oscillator offsets, the two envelope pitch modulations and the
    // vibrato amount; per voice life is not a gene (D-3) and the output
    // adjusters never move.
    const char* const pitchIds[] = { "a_freq", "b_freq", "a_mod", "b_mod", "lfo_amt" };
    bool pitchOk = true;
    for (const char* id : pitchIds)
    {
        bool found = false;
        for (std::size_t i = 0; i < kNumGenes; ++i)
            if (std::string (kGenes[i].id) == id)
                found = (kGenes[i].flags & kFlagPitch) != 0;
        if (! found)
            pitchOk = false;
    }
    for (std::size_t i = 0; i < kNumGenes; ++i)
        if (std::string (kGenes[i].id) == "vol_sus" && (kGenes[i].flags & kFlagPitch) != 0)
            pitchOk = false;   // deleted genes must not come back through flags
    for (const char* gone : { "vol_rel", "det_spread", "drift_amt", "balance", "sat_drive",
                              "fx_mix", "fx_len", "fx_damp", "fx_chors", "fx_size" })
        for (std::size_t i = 0; i < kNumGenes; ++i)
            if (std::string (kGenes[i].id) == gone)
                pitchOk = false;
    check (pitchOk, "the Rev 1 gene set is gone and the pitch flags are R7's list");
}

//==============================================================================
// 5.7.6: the output surface of the instrument is not part of the plant. These
// five must be bit identical along a branch at any length.
static void testGrowthExempt()
{
    std::printf ("growth exemption (5.7.6)\n");

    const MutationParams mp;
    const uint64_t seedId = hash::fromString ("growth-exempt");
    Genome seed {};
    Rng rng (seedId);
    randomizeGenome (seed, rng, mp.atonality);

    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);

    std::vector<std::size_t> exempt;
    for (std::size_t i = 0; i < kNumGenes; ++i)
        if ((kGenes[i].flags & kFlagNever) != 0)
            exempt.push_back (i);

    bool ok = ! exempt.empty();
    for (int branch = 0; branch < kNumBranches && ok; ++branch)
    {
        for (float d = 0.0f; d <= 1.0001f; d += 0.2f)
        {
            BranchTraits traits;
            const Genome g = resolveBranchGenome (plant, branch, d, traits);
            for (std::size_t gene : exempt)
            {
                if (g[gene] != plant.seed[gene])
                {
                    std::printf ("       branch %d at %.1f moved %s\n",
                                 branch, d, kGenes[gene].id);
                    ok = false;
                }
            }
        }
    }
    check (ok, "no branch length moves a growth exempt gene");
}

//==============================================================================
static void testCurves()
{
    std::printf ("curves\n");
    bool ok = true;

    for (std::size_t g = 0; g < kNumGenes; ++g)
    {
        const GeneInfo& gi = kGenes[g];
        std::vector<float> steps;

        for (float v = 0.0f; v <= 1.0001f; v += 0.125f)
        {
            const float value = mapGene (gi, v);
            const float back = unmapGene (gi, value);

            if (! std::isfinite (value))
            {
                std::printf ("    %s: v=%.3f -> %g is not finite\n", gi.id, v, value);
                ok = false;
                continue;
            }

            if (gi.curve == Curve::Stepped)
            {
                // 5.7.5: a stepped gene maps many normalized values onto one
                // real value, and its inverse returns that step rather than the
                // value that produced it. The step grid itself has to be exact
                // and the inverse stable, which is what this asserts.
                if (std::abs (unmapGene (gi, mapGene (gi, back)) - back) > 1.0e-3f)
                {
                    std::printf ("    %s: step %g does not invert\n", gi.id, value);
                    ok = false;
                }
                if (std::find (steps.begin(), steps.end(), value) == steps.end())
                    steps.push_back (value);
                continue;
            }

            if (std::abs (back - v) > 1.0e-3f)
            {
                std::printf ("    %s: v=%.3f -> %.6g -> %.3f\n", gi.id, v, value, back);
                ok = false;
            }
        }

        if (gi.curve == Curve::Stepped && steps.size() != gi.steps)
        {
            std::printf ("    %s: %d steps reachable, table says %u\n",
                         gi.id, static_cast<int> (steps.size()), gi.steps);
            ok = false;
        }

        const float lo = mapGene (gi, 0.0f);
        const float hi = mapGene (gi, 1.0f);
        if (std::abs (lo - gi.minValue) > 1.0e-3f || std::abs (hi - gi.maxValue) > 1.0e-3f)
        {
            std::printf ("    %s: range mismatch %g..%g\n", gi.id, lo, hi);
            ok = false;
        }
    }

    check (ok, "normalized <-> real round trip for every gene");
}

//==============================================================================
// M1's mapping layer: one place turns gene space into DSP values, and what it
// turns them into is exactly the six curation mechanisms of 5.7 that are pure
// arithmetic. These three tests are the gate for that milestone:
//
//   testMappingNeutralIsRest  - 5.7.1: every destination reads its identity
//                               value when its gene sits at the neutral value
//                               the table documents. This is "a gene at neutral
//                               changes nothing audible", stated where it can be
//                               checked exactly instead of by ear.
//   testActivationGates       - 5.7.3: the dead zones are real, for every gene
//                               the reference documents one for, at both ends.
//   testRailsAndMixWarp       - 5.7.4 and 5.7.7: flt_kf's soft rails and
//                               osc_mix's 0.79 equal power point.
//==============================================================================
static VoiceParams resolveAt (const Genome& g, int note = 60, float velocity = 1.0f)
{
    VoiceParams p;
    const MacroValues macros;          // the defaults: no macro scaling in these tests
    const BranchTraits traits;         // no structural traits at distance 0
    resolveVoiceParams (g, traits, macros, note, velocity, p);
    return p;
}

static bool near (float a, float b, float eps = 1.0e-3f) { return std::abs (a - b) <= eps; }

static void testMappingNeutralIsRest()
{
    std::printf ("neutral is the identity at every destination (5.7.1)\n");

    const Genome neutral = neutralGenome();

    // The table's neutrals are the reference's factory values (4.2), so "all
    // neutral" and "the factory state" are the same genome; say so once, loudly.
    check (near (neutral[g_osc_mix], 0.7937f, 1.0e-3f)
           && near (neutral[g_flt_type], 0.6667f, 1.0e-3f)
           && near (neutral[g_flt_freq], 0.7500f, 1.0e-3f),
           "the neutral genome is the documented factory state");

    const VoiceParams p = resolveAt (neutral);

    int bad = 0;
    auto expect = [&](const char* what, float got, float want, float eps = 1.0e-3f) {
        if (! near (got, want, eps))
        {
            std::printf ("       %s reads %.6g at neutral, identity is %.6g\n", what, got, want);
            ++bad;
        }
    };

    // Pitch and timbre offsets: nothing offset, nothing modulated.
    expect ("a_freq offset", p.aRatio, 0.0f);
    expect ("b_freq offset", p.bRatio, 0.0f);
    expect ("a_mod", p.aMod, 0.0f);
    expect ("b_mod", p.bMod, 0.0f);
    expect ("lfo amount", p.lfoAmount, 0.0f);
    expect ("fm amount", p.fmAmount, 0.0f);
    expect ("fm envelope", p.fmMod, 0.0f);
    expect ("mix envelope", p.mixMod, 0.0f);
    expect ("sub am", p.subLevel, 0.0f);
    expect ("a noise", p.aNoise, 0.0f);
    expect ("b noise", p.bNoise, 0.0f);
    expect ("noise colour offset", p.bColour, p.aColour);
    expect ("b sample hold", p.bShape, 0.0f);

    // Envelope: gated sustain is silence, the curve exponents are linear, the
    // S&H is off and velocity is ignored.
    expect ("amp sustain", p.ampSustain, 0.0f);
    expect ("vol_fade", p.volFade, 0.0f);
    // M3: the four curvature genes are exponents of the shared shape, so the
    // neutral genome sits exactly on linear, and the envelope's own duration is
    // env_time's neutral value rather than a made up ADSR.
    expect ("amp attack curvature", p.ampAttackExp, 1.0f);
    expect ("amp decay curvature", p.ampDecayExp, 1.0f);
    expect ("mod attack curvature", p.modAttackExp, 1.0f);
    expect ("mod decay curvature", p.modDecayExp, 1.0f);
    expect ("mod sample hold", p.modSampleHoldHz, 0.0f);
    expect ("mod velocity", p.modVelocity, 0.0f);

    // LFO: no onset delay.
    expect ("lfo delay", p.lfoDelaySeconds, 0.0f);

    // The mix warp puts the documented 0.7937 on the equal power point.
    expect ("mix space (equal power)", p.balance, 0.5f);

    // Filter: no separation, no key follow, no envelope, minimum resonance, and
    // the default topology step. The cutoff is a ratio of A, so it is checked as
    // one: at neutral it sits 5.5 octaves above the note (4.2's flt_freq range
    // and its 0.75 default), which is "wide open", not a fixed hertz.
    expect ("filter separation", p.filterSep, 0.0f);
    expect ("filter key follow", p.keyFollow, 0.0f);
    expect ("filter envelope", p.filterMod, 0.0f);
    expect ("resonance", p.filterQ, 0.5f);          // the SVF's quietest Q
    expect ("filter topology step", p.filterTopology, 0.6667f, 2.0e-3f);
    const float fA = 261.6256f;                     // note 60, no tuning offset
    expect ("cutoff tracks A", p.filterHz / fA, std::pow (2.0f, 5.5f), 0.02f);
    expect ("cutoff pair is one ratio", p.cutoff2Hz / p.filterHz, 1.0f);

    // Saturation, reverb and output: all off, no dB, no spread.
    expect ("saturate", p.drive, 0.0f);
    expect ("reverb mix", p.fxMix, 0.0f);
    expect ("reverb bloom", p.reverbBloomSeconds, 0.0f);
    expect ("bass shelf", p.bassDb, 0.0f);
    expect ("treble shelf", p.trebleDb, 0.0f);
    expect ("pan spread", p.panSpread, 0.0f);
    expect ("clipper", p.clipDrive, 0.0f);

    // Level compensation is relative by construction (5.6): it compares a branch
    // to the seed, so all it has to do at neutral is stay sane.
    if (! std::isfinite (p.gain) || p.gain <= 0.2f || p.gain > 1.0f)
    {
        std::printf ("       neutral gain = %g\n", p.gain);
        ++bad;
    }

    // A gene at neutral is also inaudible through the *pitch* of a played note:
    // the resolved pitch is the note itself at any key, because key follow and
    // the octave grid are both identity there.
    const VoiceParams hi = resolveAt (neutral, 84);
    if (! near (hi.freqAHz / p.freqAHz, 4.0f, 0.01f) || ! near (hi.keyFollow, 0.0f))
    {
        std::printf ("       note 84 resolves to %g Hz (want 4x %g)\n", hi.freqAHz, p.freqAHz);
        ++bad;
    }

    check (bad == 0, "every destination reads its identity value at neutral");
}

static void testActivationGates()
{
    std::printf ("activation gates (5.7.3)\n");

    struct GateCase
    {
        const char* id;
        std::size_t gene;
        float offValue;      // inside the dead zone
        float onValue;       // top of the active range
        float VoiceParams::*field;
        float onExpect;
    };

    // The six genes the reference documents gates for. Their off value is the
    // middle of their dead zone, their on value the top of the range.
    static const GateCase cases[] = {
        { "vol_sus",  g_vol_sus,  0.20f, 1.00f, &VoiceParams::ampSustain,        1.0f },
        { "vol_fade", g_vol_fade, 0.40f, 1.00f, &VoiceParams::volFade,           1.0f },
        { "mod_vel",  g_mod_vel,  0.05f, 1.00f, &VoiceParams::modVelocity,       1.0f },
        { "lfo_dly",  g_lfo_dly,  0.25f, 1.00f, &VoiceParams::lfoDelaySeconds,   1.0f },
        { "rvb_atk",  g_rvb_atk,  0.30f, 1.00f, &VoiceParams::reverbBloomSeconds, 1.0f },
    };

    int bad = 0;
    for (const GateCase& c : cases)
    {
        const GeneInfo& gi = kGenes[c.gene];

        Genome off = neutralGenome();  off[c.gene] = c.offValue;
        Genome on  = neutralGenome();  on[c.gene]  = c.onValue;

        const VoiceParams po = resolveAt (off);
        const VoiceParams pn = resolveAt (on);

        const float gotOff = po.*(c.field);
        const float gotOn  = pn.*(c.field);

        // Quiet end of the curve when the gate is shut, full value when open,
        // and monotonic between them (no "half on" dead zone).
        // "off" is defined by the curve, not by a hardcoded zero: for every
        // level gene the reference gates, the quiet end of the curve *is* off.
        if (! near (gotOff, quietEnd (gi)) || ! near (gotOn, c.onExpect))
        {
            std::printf ("       %s: gate %.2f -> %.4g, %.2f -> %.4g (want 0 and %g)\n",
                         c.id, c.offValue, gotOff, c.onValue, gotOn, c.onExpect);
            ++bad;
        }

        float last = -1.0f;
        for (float v = gi.gateLow; v <= gi.gateHigh + 1.0e-3f; v += 0.05f)
        {
            Genome g = neutralGenome();
            g[c.gene] = v;
            const float got = resolveAt (g).*(c.field);
            if (got < last - 1.0e-4f)
            {
                std::printf ("       %s: %g -> %g then %g -> %g is not monotonic\n",
                             c.id, v - 0.05f, last, v, got);
                bad++;
                break;
            }
            last = got;
        }
    }

    // mod_sh's gate is the one whose "off" is not the quiet end of the curve:
    // its curve at gene 0 is the *fastest* rate, so the destination needs
    // gateOpen() rather than the clamped remap.
    const GeneInfo& sh = kGenes[g_mod_sh];
    Genome shOff = neutralGenome();  shOff[g_mod_sh] = 0.40f;            // below the gate
    Genome shEdge = neutralGenome(); shEdge[g_mod_sh] = sh.gateLow;      // neutral == the gate edge
    Genome shOpen = neutralGenome(); shOpen[g_mod_sh] = sh.gateLow + 0.01f;  // just opened
    Genome shTop = neutralGenome();  shTop[g_mod_sh]  = 1.00f;

    const VoiceParams qOff  = resolveAt (shOff);
    const VoiceParams qEdge = resolveAt (shEdge);
    const VoiceParams qOpen = resolveAt (shOpen);
    const VoiceParams qTop  = resolveAt (shTop);

    if (! near (qOff.modSampleHoldHz, 0.0f) || ! near (qEdge.modSampleHoldHz, 0.0f)
        || qOpen.modSampleHoldHz < 5.0f || qTop.modSampleHoldHz > 0.2f
        || qOpen.modSampleHoldHz <= qTop.modSampleHoldHz)
    {
        std::printf ("       mod_sh: %.4g / %.4g / %.4g / %.4g Hz "
                     "(want off at and below the gate, then 8 Hz down to 0.1)\n",
                     qOff.modSampleHoldHz, qEdge.modSampleHoldHz,
                     qOpen.modSampleHoldHz, qTop.modSampleHoldHz);
        ++bad;
    }

    // And gates are a *reading*: nothing may bake one into a genome, because a
    // gate applied to stored values would make growth move values twice.
    Genome g = neutralGenome();
    g[g_vol_sus] = 0.2f;
    if (! near (g[g_vol_sus], 0.2f))
        ++bad;

    check (bad == 0, "the six documented dead zones are real at both ends");
}

static void testRailsAndMixWarp()
{
    std::printf ("soft rails and the mix warp (5.7.4, 5.7.7)\n");

    bool ok = true;

    // 5.7.4: "nothing below 0.25, exact above 0.75, smooth in between".
    if (! near (softRails (0.0f, 0.25f, 0.75f), 0.0f) || ! near (softRails (0.25f, 0.25f, 0.75f), 0.0f)
        || ! near (softRails (0.5f, 0.25f, 0.75f), 0.5f) || ! near (softRails (0.75f, 0.25f, 0.75f), 1.0f)
        || ! near (softRails (1.0f, 0.25f, 0.75f), 1.0f))
        ok = false;

    float last = -1.0f;
    for (float v = 0.0f; v <= 1.0001f; v += 0.02f)
    {
        const float r = softRails (v, 0.25f, 0.75f);
        if (r < last - 1.0e-4f || r < 0.0f || r > 1.0f)
            ok = false;
        last = r;
    }

    // Through the mapping layer: below the lower rail a key follow gene does
    // nothing at all, whatever its signed value says.
    Genome kf = neutralGenome();
    kf[g_flt_kf] = 0.10f;                                   // far below the lower rail
    const VoiceParams pk = resolveAt (kf, 84);
    if (! near (pk.keyFollow, 0.0f))
    {
        std::printf ("       flt_kf at %.2f on key 84 gives %.4f oct (want 0)\n", 0.10f, pk.keyFollow);
        ok = false;
    }

    Genome kfFull = neutralGenome();
    kfFull[g_flt_kf] = 0.75f;                               // rails fully open, +0.5 oct/key
    const VoiceParams pkf = resolveAt (kfFull, 84);
    if (! near (pkf.keyFollow, 0.5f * 2.0f, 5.0e-3f))      // 2 keys above middle C
    {
        std::printf ("       flt_kf at 0.75 on key 84 gives %.3f oct, want %.3f\n",
                     pkf.keyFollow, 1.0f);
        ok = false;
    }
    check (ok, "flt_kf is soft-railed: nothing under a quarter, exact over three quarters");

    // 5.7.7: the reference's 50/50 is at 0.79, not at the middle of the gene.
    ok = true;
    float worstAtNeutral = 1.0e9f;
    for (int i = 0; i < 21; ++i)
    {
        Genome g = neutralGenome();
        g[g_osc_mix] = 0.70f + 0.005f * static_cast<float> (i);   // 0.70 .. 0.80
        const VoiceParams pv = resolveAt (g);
        worstAtNeutral = std::min (worstAtNeutral, std::abs (pv.balance - 0.5f));
    }

    Genome aOnly = neutralGenome();  aOnly[g_osc_mix] = 0.0f;
    Genome bOnly = neutralGenome();  bOnly[g_osc_mix] = 1.0f;
    if (! near (resolveAt (aOnly).balance, 0.0f) || ! near (resolveAt (bOnly).balance, 1.0f))
        ok = false;

    const VoiceParams atNeutral = resolveAt (neutralGenome());
    if (! near (atNeutral.balance, 0.5f))
        ok = false;

    check (ok && worstAtNeutral < 0.02f,
           "osc_mix reaches equal power at the documented 0.79, ends stay at the ends");
}

//==============================================================================
// 5.5: atonality 0 must mean "fully snapped", 1 must mean "free". Getting that
// blend the other way round makes every branch sing a random pitch while the
// atonality macro is closed, which is the one thing the guard rail promises not
// to do, so it is checked from both ends.
static void testRatioSnapping()
{
    std::printf ("ratio snapping (5.5)\n");
    bool ok = true;
    int distinct = 0;
    float previous = 0.0f;
    float maxDeviation = 0.0f;

    for (float s = -24.0f; s <= 24.001f; s += 0.25f)
    {
        const float snapped = snapToRatio (s, 0.0f);

        // Fully snapped: the result is a table entry, so it is a fixed point of
        // the snapper and never further from s than the nearest entry can be.
        if (std::abs (snapToRatio (snapped, 0.0f) - snapped) > 1.0e-3f)
            ok = false;
        if (std::abs (snapped - s) > 2.5f)   // widest half gap in the table is 2.5
            ok = false;
        maxDeviation = std::max (maxDeviation, std::abs (snapped - s));
        if (std::abs (snapToRatio (s, 1.0f) - s) > 1.0e-4f)   // atonality 1 = free
            ok = false;

        // The blend has to move monotonically from the table entry to the raw
        // value as atonality opens.
        float lastDeviation = -1.0f;
        for (float a = 0.0f; a <= 1.0001f; a += 0.1f)
        {
            const float deviation = std::abs (snapToRatio (s, a) - snapped);
            if (deviation < lastDeviation - 1.0e-4f)
                ok = false;
            lastDeviation = deviation;
        }

        if (std::abs (snapped - previous) > 1.0e-4f)
        {
            previous = snapped;
            ++distinct;
        }
    }

    // 193 samples over +-24 semitones must collapse onto the ratio table, not
    // onto 193 different pitches.
    if (distinct < 4 || distinct > 40)
        ok = false;

    check (ok, "atonality 0 snaps to the ratio table, 1 leaves the gene free");
    std::printf ("       %d distinct offsets over 193 free values, worst pull %.2f st\n",
                 distinct, maxDeviation);

    // The snapper is not wired into the engine any more. applyAtonality used to
    // run every kFlagRatio gene through it at atonality 0.0 *whatever the
    // atonality was*, which made b_freq a ladder value at every setting - the
    // opposite of 5.5, where the snap is the atonality == 0 end of a blend. It
    // comes back in M8 as part of converge(); until then b_freq is a free
    // semitone offset bounded only by the pitch ceiling.
    {
        MutationParams mp;
        mp.atonality = 1.0f;

        Genome seed = neutralGenome();
        const float oddSemitones = 11.1f;   // not on the ladder (nearest entries are 10 and 12)
        seed[g_b_freq] = unmapGene (kGenes[g_b_freq], oddSemitones);

        PlantSnapshot plant;
        buildPlant (plant, hash::fromString ("free-b-freq"), seed, mp);

        BranchTraits traits;
        const Genome resolved = resolveBranchGenome (plant, 3, 0.0f, traits);
        const float got = mapGene (g_b_freq, resolved[g_b_freq]);

        check (std::abs (got - oddSemitones) < 0.01f,
               "nothing snaps b_freq onto the ladder at generation time any more");
        if (std::abs (got - oddSemitones) >= 0.01f)
            std::printf ("       b_freq became %.3f st\n", got);
    }
}

//==============================================================================
// 5.7.1 + 11: "a gaussian centred on each gene's default, reflected at the
// rails (spread of about a quarter of the range)... with softMin/softMax as an
// extra per-gene clamp on top". The uniform-in-the-soft-range draw this replaces
// was the reason a random seed was, on average, halfway to the extremes of every
// range at once; centring on neutral is what makes "mostly nothing" the likely
// outcome of a seed, and everything else in 5.7 assumes it.
static void testSeedsClusterAroundNeutral()
{
    std::printf ("seeds are drawn around neutral (5.7.1, 11)\n");

    constexpr int kRounds = 400;
    std::vector<double> meanDev (kNumGenes, 0.0);
    std::vector<float> maxDev (kNumGenes, 0.0f);
    bool inRange = true, deterministic = true;

    for (int round = 0; round < kRounds; ++round)
    {
        Genome g{}, again{};
        Rng r (hash::mix (static_cast<uint64_t> (round) + 7000ull));
        Rng r2 (hash::mix (static_cast<uint64_t> (round) + 7000ull));
        randomizeGenome (g, r);
        randomizeGenome (again, r2);

        for (std::size_t i = 0; i < kNumGenes; ++i)
        {
            if (! std::isfinite (g[i]) || g[i] < 0.0f || g[i] > 1.0f)
                inRange = false;
            if (g[i] != again[i])
                deterministic = false;

            const float dev = std::abs (g[i] - neutralOf (kGenes[i]));
            meanDev[i] += static_cast<double> (dev);
            maxDev[i] = std::max (maxDev[i], dev);
        }
    }

    check (inRange, "every drawn gene stays a normalized value");
    check (deterministic, "the same rng seed draws the same genome");

    // The bounded gaussian has a maximum excursion of its spread, and the soft
    // range only ever narrows it. 0.25 is the documented quarter range.
    bool spreadOk = true;
    int worstGene = 0;
    for (std::size_t i = 0; i < kNumGenes; ++i)
    {
        if ((kGenes[i].flags & kFlagPitch) != 0)
            continue;   // drawn inside the atonality band, checked elsewhere
        if (maxDev[i] > 0.251f)
        {
            spreadOk = false;
            std::printf ("       %s reached %.3f from neutral\n", kGenes[i].id, maxDev[i]);
        }
        if (maxDev[i] > maxDev[static_cast<std::size_t> (worstGene)])
            worstGene = static_cast<int> (i);
    }
    check (spreadOk, "no seed draws further from neutral than the documented spread");

    // And the distribution is centred: a uniform draw across a soft range puts
    // the mean a quarter of the range away from the middle, a gaussian around
    // neutral puts it a sixth there at most. Compare the two on genes whose soft
    // range is wide (a_form 0.05..0.95: uniform mean dev ~0.225, gaussian ~0.09).
    bool clustered = true;
    double worstMean = 0.0;
    const char* wideOnes[] = { "a_form", "b_form", "a_color", "rvb_len", "b_sh" };
    for (const char* id : wideOnes)
    {
        for (std::size_t i = 0; i < kNumGenes; ++i)
        {
            if (std::string (kGenes[i].id) != id)
                continue;
            const double mean = meanDev[i] / kRounds;
            worstMean = std::max (worstMean, mean);
            if (mean > 0.14)
            {
                clustered = false;
                std::printf ("       %s mean distance from neutral %.3f\n", id, mean);
            }
        }
    }
    check (clustered, "seeds sit near neutral on the wide ranges, not across them");
    std::printf ("       widest excursion %s at %.3f, worst mean distance on a wide range %.3f\n",
                 kGenes[static_cast<std::size_t> (worstGene)].id, maxDev[static_cast<std::size_t> (worstGene)],
                 worstMean);
}

//==============================================================================
// 5.2 step 3, new in Rev 3: growth biases displacement toward the value where
// nothing happens. With a direction of zero the term is everything, so that is
// how it is measured exactly; then the same term is checked to be a bias and not
// a clamp when the real direction pushes the other way.
static void testPullToNeutral()
{
    std::printf ("growth pulls toward neutral (5.2)\n");

    MutationParams mp;
    mp.atonality = 1.0f;
    mp.pullToNeutral = 0.6f;

    // A seed deliberately parked at the edges of a few ranges - inside each
    // gene's soft range, because softCompress anchors outside it and the pull
    // would be measured against the boundary handling instead of against itself.
    Genome seed = neutralGenome();
    seed[g_a_form]  = 0.95f;   // kGenes[a_form].softMax
    seed[g_lfo_bal] = 0.25f;   // softMin of lfo_bal
    seed[g_flt_q]   = 0.60f;   // softMax of flt_q
    seed[g_rvb_len] = 0.05f;   // softMin of rvb_len
    seed[g_b_color] = 0.30f;   // softMin of the b_color extra

    BranchDirection still{};   // zero direction: only the pull acts

    bool movesCloser = true, exactAtZero = true, exemptOk = true;
    for (float d = 0.05f; d <= 1.0001f; d += 0.25f)
    {
        const Genome out = applyDistance (seed, still, d, mp);
        for (std::size_t i = 0; i < kNumGenes; ++i)
        {
            const GeneInfo& gi = kGenes[i];
            const float before = std::abs (seed[i] - neutralOf (gi));
            const float after  = std::abs (out[i] - neutralOf (gi));

            if ((gi.flags & kFlagNever) != 0)
            {
                if (out[i] != seed[i])
                {
                    exemptOk = false;
                    std::printf ("       %s moved from %g to %g at distance %g\n",
                                 gi.id, seed[i], out[i], d);
                }
                continue;
            }

            if (before > 1.0e-4f && after >= before)
            {
                movesCloser = false;
                std::printf ("       %s: %g -> %g away from neutral at distance %g\n",
                             gi.id, before, after, d);
            }
        }
    }

    const Genome atZero = applyDistance (seed, still, 0.0f, mp);
    for (std::size_t i = 0; i < kNumGenes; ++i)
        if (atZero[i] != seed[i])
            exactAtZero = false;

    check (exactAtZero, "the pull cannot move anything at distance 0");
    check (movesCloser, "with no direction, growth walks every gene toward its neutral");
    check (exemptOk, "and never touches a growth exempt gene (5.7.6)");

    // It is a bias, not a clamp: with the real directions of a plant, growth
    // still spreads, it just ends up nearer the middle on average.
    const uint64_t seedId = hash::fromString ("pull-comparison");
    Genome spreadSeed{};
    Rng rng (seedId);
    randomizeGenome (spreadSeed, rng, mp.atonality);

    float sumNoPull = 0.0f, sumPull = 0.0f;
    MutationParams off = mp;  off.pullToNeutral = 0.0f;
    PlantSnapshot a, b;
    buildPlant (a, seedId, spreadSeed, off);
    buildPlant (b, seedId, spreadSeed, mp);

    for (int branch = 0; branch < kNumBranches; ++branch)
    {
        for (std::size_t i = 0; i < kNumGenes; ++i)
        {
            if ((kGenes[i].flags & kFlagNever) != 0)
                continue;
            sumNoPull += std::abs (applyDistance (spreadSeed, a.direction (branch), 1.0f, off)[i]
                                   - neutralOf (kGenes[i]));
            sumPull   += std::abs (applyDistance (spreadSeed, b.direction (branch), 1.0f, mp)[i]
                                   - neutralOf (kGenes[i]));
        }
    }

    const float per = 1.0f / static_cast<float> (static_cast<int> (kNumBranches)
                                                   * static_cast<int> (kNumGenes));
    std::printf ("       mean distance from neutral at length 1: %.4f without the pull, %.4f with\n",
                 sumNoPull * per, sumPull * per);

    check (sumPull * per < sumNoPull * per * 0.9f && sumPull > 1.0f,
           "the pull moves the plant toward the middle but keeps it spread out");
    check (mp.pullToNeutral > 0.0f && mp.pullToNeutral < 1.0f,
           "and the shipped default is a nudge, not a clamp");
}

//==============================================================================
static void testGenome (const MutationParams& mp)
{
    std::printf ("genome / mutation\n");

    const uint64_t seedId = hash::fromString ("test-seed");
    Genome seed{};
    Rng rng (seedId);
    randomizeGenome (seed, rng);

    PlantSnapshot a, b;
    buildPlant (a, seedId, seed, mp);
    buildPlant (b, seedId, seed, mp);

    bool identical = true;
    for (int br = 0; br < kNumBranches; ++br)
    {
        for (std::size_t v = 0; v < kNumDirections; ++v)
        {
            for (std::size_t g = 0; g < kNumGenes; ++g)
            {
                if (a.direction (br, static_cast<int> (v)).dir[g]
                    != b.direction (br, static_cast<int> (v)).dir[g])
                    identical = false;
            }
        }
    }
    check (identical, "same seed id gives bit identical branch directions");

    bool zeroIdentity = true;
    for (int br = 0; br < kNumBranches; ++br)
    {
        BranchTraits traits;
        const Genome g0 = resolveBranchGenome (a, br, 0.0f, traits);
        // 5.1.3, against the plant's own seed: buildPlant applies the atonality
        // ceiling to the seed, so "the seed" from the audio thread's point of
        // view is the gated one. Distance 0 still returns it bit exactly.
        for (std::size_t g = 0; g < kNumGenes; ++g)
            if (g0[g] != a.seed[g])
                zeroIdentity = false;
    }
    check (zeroIdentity, "distance 0 returns the plant's seed exactly");

    // Deviation must grow with distance (continuity + seed anchoring).
    bool monotonic = true;
    for (int br = 0; br < kNumBranches; ++br)
    {
        float previous = 0.0f;
        for (float d = 0.0f; d <= 1.0001f; d += 0.1f)
        {
            const Genome g = applyDistance (seed, a.direction (br), d, mp);
            float deviation = 0.0f;
            for (std::size_t gi = 0; gi < kNumGenes; ++gi)
            {
                if (! std::isfinite (g[gi]) || g[gi] < 0.0f || g[gi] > 1.0f)
                    monotonic = false;
                deviation += std::abs (g[gi] - seed[gi]);
            }

            if (d > 0.0f && deviation <= previous)
                monotonic = false;
            previous = deviation;
        }
    }
    check (monotonic, "deviation from the seed grows monotonically with distance");

    // Guard rails over many seeds.
    int usable = 0, total = 0;
    bool safe = true;
    for (uint64_t s = 0; s < 60; ++s)
    {
        Genome g2{};
        Rng r2 (hash::mix (s + 1234));
        randomizeGenome (g2, r2);

        PlantSnapshot p;
        buildPlant (p, s + 1234, g2, mp);

        for (int br = 0; br < kNumBranches; ++br)
        {
            BranchTraits traits;
            const Genome resolved = resolveBranchGenome (p, br, 1.0f, traits);
            const float score = safetyScore (resolved);

            ++total;
            if (score > 0.5f)
                ++usable;

            for (std::size_t gi = 0; gi < kNumGenes; ++gi)
            {
                if (! std::isfinite (resolved[gi]) || resolved[gi] < 0.0f || resolved[gi] > 1.0f)
                    safe = false;
            }

            if (resolved[g_flt_q] > 1.0f || resolved[g_vol_sus] < 0.0f)
                safe = false;
        }
    }
    check (safe, "all resolved genomes stay finite and inside [0,1]");
    std::printf ("       usable branches at distance 1: %d / %d\n", usable, total);
    // M3 raised this from "majority" to the hard gate the plan asks for: every
    // resolved branch has to pass the recalibrated safety score, because the
    // score is now the thing that reads gated sustain and duration (5.6's
    // silence rejection is load bearing precisely because vol_sus has a gate and
    // env_time is a length). The ratio stays printed, not just the verdict.
    check (usable == total, "every resolved branch passes the safety score");
}

//==============================================================================
// Reference RBJ lowpass, to confirm the trapezoidal SVF is correct.
struct RbjLowpass
{
    double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void set (double fc, double q, double fs)
    {
        const double w = 2.0 * 3.14159265358979 / fs * fc;
        const double cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - cw) / 2.0 / a0;
        b1 = (1.0 - cw) / a0;
        b2 = b0;
        a1 = -2.0 * cw / a0;
        a2 = (1.0 - alpha) / a0;
    }

    double process (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

static double sineMagnitude (TrapezoidSVF& svf, double fc, double q, double fs, double freq, int cycles)
{
    svf.setSampleRate (fs);
    svf.setCutoffQ (static_cast<float> (fc), static_cast<float> (q));

    const int numSamples = static_cast<int> (fs * cycles / freq) * static_cast<int> (freq) / static_cast<int> (freq);
    const int n = static_cast<int> (fs * 2.0);

    double peak = 0.0, prevPeak = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double x = std::sin (2.0 * 3.14159265358979 * freq * static_cast<double> (i) / fs);
        const double y = svf.process (static_cast<float> (x), 1.0f, 0.0f, 0.0f);
        if (! std::isfinite (y))
            return -1.0;
        if (i > n / 2)
            peak = std::max (peak, std::abs (y));
        prevPeak = peak;
    }
    (void) prevPeak; (void) numSamples; (void) cycles;
    return peak;
}

static void testFilter()
{
    std::printf ("filter\n");

    const double fs = 48000.0;
    TrapezoidSVF svf;

    double worstDb = 0.0;
    const double testFreqs[] = { 100.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0 };
    for (double f : testFreqs)
    {
        RbjLowpass ref;
        ref.set (1000.0, 0.707, fs);

        double refPeak = 0.0, svfPeak = 0.0;
        svf.setSampleRate (fs);
        svf.setCutoffQ (1000.0f, 0.707f);

        const int n = static_cast<int> (fs * 2);
        for (int i = 0; i < n; ++i)
        {
            const double x = std::sin (2.0 * 3.14159265358979 * f * static_cast<double> (i) / fs);
            const double yr = ref.process (x);
            const double ys = svf.process (static_cast<float> (x), 1.0f, 0.0f, 0.0f);
            if (i > n / 2)
            {
                refPeak = std::max (refPeak, std::abs (yr));
                svfPeak = std::max (svfPeak, std::abs (ys));
            }
        }

        const double db = 20.0 * std::log10 ((svfPeak + 1.0e-9) / (refPeak + 1.0e-9));
        worstDb = std::max (worstDb, std::abs (db));
        std::printf ("       fc 1000 Hz, response at %6.0f Hz: %+6.2f dB vs biquad\n", f, db);
    }
    check (worstDb < 1.0, "trapezoidal SVF matches a reference lowpass within 1 dB");

    const double high = sineMagnitude (svf, 15000.0, 8.0, fs, 3000.0, 1);
    check (high > 0.0 && high < 20.0, "SVF stays stable at fc 15 kHz, Q 8");
}

/// A scratch file for the protocol tests. Nothing large is kept: design 13 says
/// the goldens are generated, not committed.
static std::string tempFilePath (const std::string& name)
{
    return "/tmp/" + name + "-" + std::to_string (static_cast<long> (::getpid ())) + ".wav";
}

static float dbDifference (float a, float b)
{
    if (a <= 0.0f || b <= 0.0f)
        return 999.0f;
    return std::abs (20.0f * std::log10 (a / b));
}

//==============================================================================
// M1's gate, the audio half: "a gene at neutral changes nothing audible". The
// mapping half is testMappingNeutralIsRest; this is the same claim made on the
// output, using the one gate whose destination already exists (vol_sus). The
// genome is planted as the seed at branch length 0, which resolveBranchGenome
// returns bit exactly, so what is heard is exactly what was asked for.
static void testNeutralGateIsAudible()
{
    std::printf ("a gene at neutral is inaudible, its gate is audible (5.7.3)\n");

    MutationParams mp;
    mp.atonality = 1.0f;   // nothing converged: the genome below is what is heard

    auto plantOf = [&](const Genome& seed) {
        PlantSnapshot plant;
        buildPlant (plant, hash::fromString ("neutral-gate"), seed, mp);
        return plant;
    };

    const float sr = 48000.0f;
    // Long enough that the difference between "no sustain" and "sustain" is
    // inside the held part of the note: with duration now a gene, the shape of
    // a neutral plant runs 1.2 s, and a 0.8 s render would measure the attack
    // of both cases and call them the same sound.
    const float seconds = 3.0f;

    const Genome base = neutralGenome();
    const Rendered neutral = renderNote (plantOf (base), 0, 0.0f, 60, 0.9f, sr, seconds);

    // Inside the dead zone of the same gate: the same sound, sample for sample.
    Genome dead = base;
    dead[g_vol_sus] = 0.2f;
    const Rendered inDeadZone = renderNote (plantOf (dead), 0, 0.0f, 60, 0.9f, sr, seconds);

    // Open the gate and the note sustains, which is a different sound.
    Genome open = base;
    open[g_vol_sus] = 0.9f;
    const Rendered opened = renderNote (plantOf (open), 0, 0.0f, 60, 0.9f, sr, seconds);

    std::printf ("       vol_sus 0.50/0.20/0.90 -> rms %.5f / %.5f / %.5f, peak %.3f/%.3f/%.3f\n",
                 neutral.m.rms, inDeadZone.m.rms, opened.m.rms,
                 neutral.m.peak, inDeadZone.m.peak, opened.m.peak);

    bool ok = neutral.m.finite && inDeadZone.m.finite && opened.m.finite;

    // The neutral plant has to make a sound at all, or "identical" is vacuous.
    if (neutral.m.rms < 1.0e-5f)
        ok = false;

    // A gene inside its dead zone cannot reach the output: not "similar", but
    // bit identical, because the mapping layer resolves both to the same value.
    if (inDeadZone.m.rms != neutral.m.rms || inDeadZone.m.peak != neutral.m.peak)
        ok = false;

    // While opening the gate is a change a listener would call loud.
    if (dbDifference (opened.m.rms, neutral.m.rms) < 1.0f)
        ok = false;

    check (ok, "dead zone is bit identical to neutral; opening the gate is audible");
}

//==============================================================================
// 5.3: "Pitch is played as normal. The branch changes timbre and modulation,
// not the played pitch, except through the oscillator offset genes." A note has
// to hold the pitch it was pressed at. The mod envelope used to sweep
// oscillator pitch by up to two thirds of an octave while it attacked, in
// whichever direction the sign of a_mod happened to point, which is what made
// every note sound randomly out of tune. The genome below tunes both
// oscillators to the pressed note with no vibrato, no drift and no detune, and
// asks for a large mod amount: if modulation reaches pitch at all, this shows it.
//
// Read this test as provisional (Docs/Rev3-Alignment-Plan.md 6.1): a_mod and
// b_mod are documented *pitch* modulation now (decision R2), so with atonality 0
// they are converged to neutral and the assertion above becomes vacuous. It is
// rewritten, not kept, in M4, the milestone that lands that behaviour.
//==============================================================================
// Atonality is a generation time ceiling on every pitch capable gene (a_freq,
// b_freq, a_mod, b_mod, lfo_amt): at 0 the plant cannot touch pitch at all, at
// 1 the whole range is available, and in between nothing may sit further from
// its neutral point than the ceiling allows. It is applied to the seed, to the
// branch directions and to the resolved genome, so it has to be idempotent:
// planting a branch feeds a gated genome back in as the next seed. Note that
// since M1 the ceiling is the *only* thing that keeps b_freq tonal: the
// unconditional ratio snap of 5.5 is gone (it was applied at every atonality),
// and the blended version arrives with M8 (R7, deviation D-4).
static void testAtonalityCeiling()
{
    std::printf ("atonality ceiling (5.5)\n");

    // R7: atonality converges the oscillator offsets and the two envelope pitch
    // modulations (plus, from M8, the ratio snap on b_freq). Per voice detune and
    // drift are not genes any more (D-3).
    const std::size_t pitchGenes[] = { g_a_freq, g_b_freq, g_a_mod, g_b_mod, g_lfo_amt };
    const float levels[] = { 0.0f, 0.25f, 1.0f };

    bool ok = true;
    float widestAtOne = 0.0f;

    for (int level = 0; level < 3; ++level)
    {
        const float at = levels[level];

        MutationParams mp;
        mp.atonality = at;

        const uint64_t seedId = hash::fromString ("atonality-ceiling");
        Genome seed {};
        Rng rng (seedId);
        randomizeGenome (seed, rng, at);

        PlantSnapshot plant;
        buildPlant (plant, seedId, seed, mp);

        float widest = 0.0f;

        for (int branch = 0; branch < kNumBranches; ++branch)
        {
            for (float d = 0.0f; d <= 1.0001f; d += 0.25f)
            {
                BranchTraits traits;
                const Genome g = resolveBranchGenome (plant, branch, d, traits);

                Genome twice = g;
                applyAtonality (twice, at);
                applyAtonality (twice, at);
                for (std::size_t gene : pitchGenes)
                {
                    if (std::abs (twice[gene] - g[gene]) > 1.0e-5f)
                        ok = false;
                }

                for (std::size_t gene : pitchGenes)
                {
                    // The ceiling is expressed in gene space around the neutral
                    // value the table documents (4.3), not inferred from curves.
                    const GeneInfo& gi = kGenes[gene];
                    const float span = gi.neutral > 0.0f ? std::max (gi.neutral, 1.0f - gi.neutral)
                                                        : 1.0f;
                    widest = std::max (widest, std::abs (g[gene] - gi.neutral) / span);
                }
            }
        }

        std::printf ("       atonality %.2f -> furthest pitch gene at %.3f of its range\n",
                     at, widest);

        if (level == 0 && widest > 1.0e-5f)
            ok = false;
        if (level > 0 && widest > at + 1.0e-4f)
            ok = false;
        if (level == 2)
            widestAtOne = widest;
    }

    // Atonality 1 must actually leave something to modulate.
    if (widestAtOne < 0.05f)
        ok = false;

    check (ok, "atonality bounds every pitch capable gene at generation time");
}

static int estimatePeriod (const std::vector<float>& mono, int start, int length,
                           int minLag, int maxLag)
{
    int best = minLag;
    double bestScore = -1.0e300;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < length; ++i)
        {
            s += static_cast<double> (mono[static_cast<std::size_t> (start + i)])
               * static_cast<double> (mono[static_cast<std::size_t> (start + i + lag)]);
        }
        if (s > bestScore)
        {
            bestScore = s;
            best = lag;
        }
    }

    return best;
}

static void testPitchStability()
{
    std::printf ("pitch stability (5.3)\n");

    MutationParams mp;
    mp.atonality = 0.0f;   // nothing in the plant may touch the pitch

    const uint64_t seedId = hash::fromString ("pitch-stability");
    Genome seed {};
    Rng rng (seedId);
    randomizeGenome (seed, rng);

    seed[g_a_freq]     = 0.5f;   // stepped middle = 0 semitones, both oscillators
    seed[g_b_freq]     = 0.5f;
    seed[g_lfo_amt]    = 0.5f;   // bipolar middle = no vibrato
    seed[g_fm_amt]     = 0.5f;
    seed[g_sub_am]     = 0.0f;
    seed[g_a_noise]    = 0.0f;
    seed[g_b_noise]    = 0.0f;
    seed[g_env_loop]   = 0.0f;
    seed[g_a_mod]      = 0.9f;   // large mod amount, both oscillators
    seed[g_b_mod]      = 0.9f;
    seed[g_mod_atk]    = 0.4f;   // peaks tens of ms after the note, decays over ~0.35 s
    seed[g_mod_dcy]    = 0.55f;
    seed[g_vol_atk]    = 0.05f;

    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);

    const float sampleRate = 48000.0f;
    const int block = 128;
    const int numSamples = static_cast<int> (sampleRate);
    const int note = 60;
    const double expectedPeriod = static_cast<double> (sampleRate) / 261.6256;

    const int window = static_cast<int> (sampleRate * 0.05);
    const int windowA = static_cast<int> (sampleRate * 0.06);
    const int windowB = static_cast<int> (sampleRate * 0.55);

    bool ok = true;
    float worstCents = 0.0f;

    for (int branch = 0; branch < kNumBranches; ++branch)
    {
        PlantHandoff handoff;
        auto& target = handoff.beginWrite();
        target = plant;
        handoff.publish();

        Synth synth;
        synth.prepare (sampleRate, block);
        synth.attachPlant (&handoff);

        PlayState state;
        state.macros.wheel = 1.0f;
        state.macros.wheelScale = 1.0f;
        state.macros.volume = 0.7f;
        for (int b = 0; b < kNumBranches; ++b)
            state.branchLength[static_cast<std::size_t> (b)] = (b == branch) ? 0.85f : 0.0f;
        synth.setPlayState (state);

        std::vector<float> left (static_cast<std::size_t> (numSamples), 0.0f);
        std::vector<float> right (static_cast<std::size_t> (numSamples), 0.0f);

        for (int offset = 0; offset < numSamples; offset += block)
        {
            const int n = std::min (block, numSamples - offset);
            if (offset == 0)
                synth.addNoteOn (note, 0.9f, 8);
            if (offset == static_cast<int> (sampleRate) * 7 / 10)
                synth.addNoteOff (note, 0);

            float* channels[2] = { left.data() + static_cast<std::size_t> (offset),
                                   right.data() + static_cast<std::size_t> (offset) };
            synth.render (channels, 2, n);
        }

        std::vector<float> mono (static_cast<std::size_t> (numSamples), 0.0f);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<std::size_t> (i)] = 0.5f * (left[static_cast<std::size_t> (i)]
                                                        + right[static_cast<std::size_t> (i)]);

        const int periodA = estimatePeriod (mono, windowA, window, 40, 700);
        const int periodB = estimatePeriod (mono, windowB, window, 40, 700);

        // The two windows of one note must agree, and the pitch found has to be
        // the pressed note (or a harmonic of it, which the autocorrelation may
        // lock onto for a bright timbre).
        const double driftCents = 1200.0 * std::log10 (static_cast<double> (periodB)
                                                       / static_cast<double> (periodA));
        worstCents = std::max (worstCents, std::abs (static_cast<float> (driftCents)));

        bool onNote = false;
        for (int harmonic = 1; harmonic <= 4; ++harmonic)
        {
            if (std::abs (static_cast<double> (periodA) * harmonic - expectedPeriod)
                < 0.02 * expectedPeriod)
                onNote = true;
        }

        if (std::abs (driftCents) > 15.0f || ! onNote)
        {
            std::printf ("       branch %d: period %d -> %d (%.1f cents), expected %.0f\n",
                         branch, periodA, periodB, driftCents, expectedPeriod);
            ok = false;
        }
    }

    check (ok, "a held note keeps the pitch it was pressed at");
    std::printf ("       worst pitch movement while held: %.1f cents\n", worstCents);
}

//==============================================================================
// M2: the design doc 13 protocol itself lives in Tests/Render.{h,cpp}. What is
// tested here is three things, in the order that makes the rest cheap:
//
//   testWavFileIsAFile      - the header is the bytes a host expects, and a
//                             broken file is an error rather than silence;
//   testMetricsOnKnownSignals - every metric against a signal whose answer is
//                             known by construction. A metric that has not been
//                             checked this way is a number, not a measurement;
//   testScenePlanIsSeeded   - the density semantics of 13, deterministically.
//
// Then the audio baseline:
//
//   testM1BaselineAudio     - the 12 numbers Implementation-Status section 11
//                             records, reproduced through the new harness. This
//                             is what proves M2 changed no sample, and it also
//                             pins the discovery below.
//   testBranchBatch         - 12 branches x lengths {0,.25,.5,.75,1} with the
//                             full metric set. The baseline M3-M7 are judged
//                             against (design 13's batch mode).
//   testGoldenProtocol      - createTest / performTest at five sample rates,
//                             with the tolerance honoured.
//   testMassiveSoak         - a short version of the 13 soak; the full 10 000
//                             round run is a command line thing (see main).
//
// The discovery M2 made on the way, and the reason testM1BaselineAudio passes
// *without* a branch length: the previous harness built a PlayState, filled it
// in, and never handed it to the Synth. Every "per branch" number in the M0 and
// M1 sections of Implementation-Status is therefore the seed played at twelve
// different notes at the plugin's default macros, not twelve grown branches.
// The branch-vs-seed spread has never been measured before this milestone, and
// testBranchBatch is the first time it is.
//==============================================================================

static void testWavFileIsAFile()
{
    std::printf ("wav header and reader (13)\n");

    const std::string path = tempFilePath ("genomosh_header");

    std::vector<float> l { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.25f, -0.25f, 0.0f };
    std::vector<float> r (l.size(), 0.0f);

    std::string error;
    const bool written = wav::writeFloat (path, 44100.0, l, r, error);
    check (written, "a wav file can be written");

    // Byte for byte what a host expects: WAVE_FORMAT_PCM, 16 bit, stereo,
    // 44 100 Hz, canonical 44 byte header. RIFF sizes are the frame count and
    // channel width the reader will claim, so a wrong byte here is exactly how
    // "the suite passed" and "the file is silence" happen together.
    static const uint8_t expected[44] = {
        'R', 'I', 'F', 'F',  68,   0,   0,  0,  'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ',  16,   0,   0,  0,  0x01, 0x00, 0x02, 0x00,
        0x44, 0xAC, 0x00, 0x00,     0x10, 0xB1, 0x02, 0x00, 0x04, 0x00, 0x10, 0x00,
        'd', 'a', 't', 'a',  32,   0,   0,  0
    };

    std::FILE* f = std::fopen (path.c_str(), "rb");
    uint8_t header[44] = {};
    if (f != nullptr)
    {
        const size_t got = std::fread (header, 1, 44, f);
        std::fclose (f);
        check (got == 44 && std::memcmp (header, expected, 44) == 0,
               "the file starts with the 44 header bytes a PCM host expects");
    }
    else
        check (false, "the file can be reopened for reading");

    // Read back, and compare in the domain the file is in. The quantiser is the
    // only lossy step in the protocol, and performTest compares both sides
    // through it so that "bit exact" is a thing that can actually be true.
    wav::Data read;
    if (wav::readFloat (path, read, error))
    {
        const std::vector<int16_t> expectedPcm = wav::quantise (l, r);
        std::vector<int16_t> gotPcm (read.left.size() * 2);
        for (size_t i = 0; i < read.left.size(); ++i)
        {
            gotPcm[i * 2] = static_cast<int16_t> (std::lround (read.left[i] * 32767.0f));
            gotPcm[i * 2 + 1] = static_cast<int16_t> (std::lround (read.right[i] * 32767.0f));
        }
        check (read.sampleRate == 44100 && read.channels == 2 && read.left.size() == l.size(),
               "the reader recovers the rate, the channel count and the frame count");
        check (gotPcm == expectedPcm, "reading the file returns exactly what was written");
    }
    else
    {
        check (false, "the reader can read the file it wrote");
    }

    // Not a wav, and a wav with its body cut off: both are errors. A reader
    // that returns an empty buffer for these would let every level check below
    // pass on a file containing nothing.
    {
        const std::string junk = tempFilePath ("genomosh_junk");
        std::FILE* j = std::fopen (junk.c_str(), "wb");
        if (j != nullptr)
        {
            const char* text = "this is not a wav file at all, not even a little";
            std::fwrite (text, 1, std::strlen (text), j);
            std::fclose (j);
        }
        std::vector<int16_t> pcm;
        int ch = 0, sr = 0;
        check (! wav::read (junk, pcm, ch, sr, error), "a file that is not a wav is refused, not read as silence");

        const std::string chopped = tempFilePath ("genomosh_chopped");
        std::FILE* c = std::fopen (chopped.c_str(), "wb");
        if (c != nullptr)
        {
            std::fwrite (expected, 1, 20, c);
            std::fclose (c);
        }
        check (! wav::read (chopped, pcm, ch, sr, error), "a truncated wav is refused");
    }

    std::remove (path.c_str());
}

//==============================================================================
// The metrics, measured against signals built from the answer. This is the part
// of M2 that makes every later audio number mean something: a centroid, a
// flatness, a fundamental or an attack time that has not been checked against a
// known case is decoration.
//==============================================================================
namespace
{
std::vector<float> makeSine (double sampleRate, double seconds, double hz, double amplitude = 0.5)
{
    std::vector<float> out (static_cast<size_t> (sampleRate * seconds));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        out[i] = static_cast<float> (amplitude * std::sin (2.0 * 3.14159265358979 * hz * t));
    }
    return out;
}

SceneNote heldNote (int midi, double sampleRate, double startSeconds, double holdSeconds)
{
    SceneNote n;
    n.midiNote = midi;
    n.velocity = 0.9f;
    n.startSample = static_cast<int> (startSeconds * sampleRate);
    n.endSample = static_cast<int> ((startSeconds + holdSeconds) * sampleRate);
    return n;
}

/// A note whose amplitude envelope is written down by hand, so the articulation
/// metric has a fixed answer to be wrong or right about.
std::vector<float> shapedTone (double sampleRate, double hz, double attack, double decayTo,
                               double sustain, double keyDown, double release)
{
    const double total = keyDown + release + 0.5;
    std::vector<float> out (static_cast<size_t> (sampleRate * total), 0.0f);

    for (size_t i = 0; i < out.size(); ++i)
    {
        const double t = static_cast<double> (i) / sampleRate;
        double e = 0.0;
        if (t < attack)
            e = attack > 0.0 ? t / attack : 1.0;
        else if (t < attack + 0.1)
            e = 1.0 + (sustain - 1.0) * ((t - attack) / 0.1) * decayTo;
        else if (t < keyDown)
            e = 1.0 + (sustain - 1.0) * decayTo;
        else if (t < keyDown + release)
            e = (1.0 + (sustain - 1.0) * decayTo) * (1.0 - (t - keyDown) / release);

        out[i] = static_cast<float> (e * 0.5 * std::sin (2.0 * 3.14159265358979 * hz * t));
    }
    return out;
}
} // namespace

static void testMetricsOnKnownSignals()
{
    std::printf ("the metrics against known signals (13)\n");

    const double sr = 48000.0;

    // 1. A pure tone: the fundamental is the tone, the centroid is the tone,
    //    the flatness is near zero and nothing is off the harmonic grid.
    {
        const double hz = 440.0;
        const auto mono = makeSine (sr, 1.0, hz);
        std::vector<SceneNote> plan { heldNote (69, sr, 0.0, 1.0) };

        const AudioMetrics m = measureAudio (mono, mono, sr, plan, true);
        const NoteMeasurement& n = m.notes.front();

        std::printf ("       440 Hz sine: f0 %.1f Hz, centroid %.1f Hz, flatness %.4f, "
                     "off grid %.4f\n",
                     n.f0Hz, n.centroidHz, n.flatness, n.inharmonicRatio);

        check (m.notesMeasured == 1, "the note the plan asked for is the note that was measured");
        // A single tone has no unique fundamental (any divisor of it explains
        // the energy), so what is asserted is that the grid the sieve uses
        // lands within the sieve's own tolerance. The next check, "no off grid
        // energy", is the one that shows whether that is good enough.
        check (n.f0Hz > 0.0 && std::abs (n.f0Hz - hz) < 0.03 * hz,
               "the grid found for a lone tone is inside the sieve's tolerance of it");
        check (std::abs (n.centroidHz - hz) < 0.1 * hz, "the spectral centroid of a tone is the tone");
        check (n.flatness < 0.15f, "a tone is not flat");
        check (n.inharmonicRatio < 0.05f, "a tone has no off grid energy to call aliasing");
    }

    // 2. A harmonic stack: same answers, and a realistic timbre for the sieve.
    // 3. Noise: no fundamental to have a grid, high flatness, centroid mid band.
    {
        std::vector<float> mono (static_cast<size_t> (sr * 1.0));
        Rng rng (hash::fromString ("white-noise-metric"));
        for (auto& s : mono)
            s = rng.range (-0.5f, 0.5f);

        std::vector<SceneNote> plan { heldNote (60, sr, 0.0, 1.0) };
        const AudioMetrics m = measureAudio (mono, mono, sr, plan, true);
        const NoteMeasurement& n = m.notes.front();

        std::printf ("       white noise: f0 %.1f, centroid %.1f Hz, flatness %.4f, unvoiced %d\n",
                     n.f0Hz, n.centroidHz, n.flatness, m.notesUnvoiced);

        check (n.flatness > 0.35f, "noise is flat");
        check (m.notesUnvoiced == 1 && m.inharmonicRatio == 0.0f,
               "noise gets no harmonic grid instead of being scored as pure aliasing");
        check (n.centroidHz > 0.2 * (sr * 0.5) && n.centroidHz < 0.6 * (sr * 0.5),
               "the centroid of white noise sits in the middle of the band");
    }

    // 4. Off grid energy is the aliasing estimate. One inharmonic partial at
    //    comparable level must show up; a folded (already aliased) component
    //    shows up as the same thing, which is the point of the metric.
    {
        auto addStack = [&](double extraHz, double extraLevel)
        {
            std::vector<float> mono (static_cast<size_t> (sr * 1.0), 0.0f);
            const double f0 = 220.0;
            for (int h = 1; h <= 8; ++h)
            {
                const double amp = 0.3 / static_cast<double> (h);
                for (size_t i = 0; i < mono.size(); ++i)
                {
                    const double t = static_cast<double> (i) / sr;
                    mono[i] += static_cast<float> (amp * std::sin (2.0 * 3.14159265358979
                                                                  * f0 * h * t));
                }
            }
            if (extraHz > 0.0)
            {
                for (size_t i = 0; i < mono.size(); ++i)
                {
                    const double t = static_cast<double> (i) / sr;
                    // A component above Nyquist is sampled, not skipped: the
                    // samples are a lower frequency, which is what aliasing is.
                    mono[i] += static_cast<float> (extraLevel * std::sin (2.0 * 3.14159265358979
                                                                         * extraHz * t));
                }
            }
            std::vector<SceneNote> plan { heldNote (57, sr, 0.0, 1.0) };
            return measureAudio (mono, mono, sr, plan, true);
        };

        const AudioMetrics clean = addStack (0.0, 0.0);
        const AudioMetrics offGrid = addStack (1450.0, 0.25);      // not a multiple of 220
        const AudioMetrics folded = addStack (26150.0, 0.25);      // lands on 21850 at 48 kHz

        std::printf ("       220 Hz stack: off grid %.4f; +1450 Hz -> %.4f; +26.15 kHz (folds "
                     "to 21.85 kHz) -> %.4f\n",
                     clean.inharmonicRatio, offGrid.inharmonicRatio, folded.inharmonicRatio);

        const float stackF0 = clean.notes.empty() ? 0.0f : clean.notes.front().f0Hz;
        check (stackF0 > 217.0f && stackF0 < 223.0f, "the grid of a harmonic stack is its fundamental");
        check (clean.inharmonicRatio < 0.1f, "a band limited stack is on its grid");
        check (offGrid.inharmonicRatio > 0.15f, "an inharmonic partial is seen as off grid energy");
        check (folded.inharmonicRatio > 0.15f, "energy that folded back from above Nyquist is seen too");
    }

    // 5. Articulation: attack, sustain level, release, and the case M3 makes
    //    real - a note that ends by itself while the key is still down.
    {
        std::vector<SceneNote> plan { heldNote (60, sr, 0.0, 0.6) };

        const auto held = shapedTone (sr, 300.0, 0.02, 1.0, 0.4, 0.6, 0.10);
        const AudioMetrics h = measureAudio (held, held, sr, plan, true);
        const NoteMeasurement& n = h.notes.front();

        std::printf ("       20 ms attack / -8 dB hold / 100 ms release: attack %.4f s, "
                     "hold %+.1f dB, release %.4f s, sound over at %.3f s, self ended %d\n",
                     n.attackSeconds, n.holdLevelDb, n.releaseSeconds, n.endSeconds,
                     n.selfEnded ? 1 : 0);

        check (n.measured && n.reachedPeak, "a held note is measured as held");
        check (n.attackSeconds > 0.005f && n.attackSeconds < 0.035f, "the measured attack tracks the real one");
        check (n.holdLevelDb < -6.5f && n.holdLevelDb > -9.5f, "the measured hold level is the sustain level");
        check (n.releaseSeconds > 0.05f && n.releaseSeconds < 0.16f, "the measured release tracks the real one");
        check (! n.selfEnded, "a note held to the key up is not called self ended");

        // Same note, but the sound dies ~120 ms in while the key stays down:
        // that is vol_fade / duration in M3, and it has to be distinguishable.
        const auto selfEnded = shapedTone (sr, 300.0, 0.02, 1.0, 0.0, 0.6, 0.0);
        std::vector<SceneNote> shortPlan { heldNote (60, sr, 0.0, 0.6) };
        const AudioMetrics s = measureAudio (selfEnded, selfEnded, sr, shortPlan, true);
        const NoteMeasurement& sn = s.notes.front();

        std::printf ("       sound gone after ~120 ms with the key down: self ended %d, sound over "
                     "at %.3f s, release %.3f s\n",
                     sn.selfEnded ? 1 : 0, sn.endSeconds, sn.releaseSeconds);

        check (sn.selfEnded && sn.endSeconds > 0.05f && sn.endSeconds < 0.45f,
               "a note that ends before the key up is reported as such");
        check (s.selfEndedNotes == 1, "the scene count agrees with the note count");
    }

    // 6. Silence and DC, the two checks that keep every other number honest.
    {
        std::vector<float> zeros (static_cast<size_t> (sr), 0.0f);
        const AudioMetrics z = measureAudio (zeros, zeros, sr, {});
        check (z.silent && z.peak == 0.0f, "silence is detected as silence");

        std::vector<float> offset (static_cast<size_t> (sr), 0.1f);
        const AudioMetrics o = measureAudio (offset, offset, sr, {});
        check (std::abs (o.dc - 0.1f) < 0.001f && o.finite, "a DC offset is measured as a DC offset");

        std::vector<float> nan (static_cast<size_t> (sr), 1.0f);
        nan[100] = std::numeric_limits<float>::quiet_NaN ();
        const AudioMetrics nn = measureAudio (nan, nan, sr, {});
        check (! nn.finite, "a NaN anywhere in the render is caught");
    }
}

//==============================================================================
static void testScenePlanIsSeeded()
{
    std::printf ("the note generator and its density (13)\n");

    const double sr = 44100.0;
    const uint64_t id = hash::fromString ("scene-plan");

    const auto a = planScene (id, sr, 60.0, 10.0);
    const auto b = planScene (id, sr, 60.0, 10.0);

    check (a.size() == b.size()
           && std::equal (a.begin(), a.end(), b.begin(),
                          [] (const SceneNote& x, const SceneNote& y)
                          { return x.midiNote == y.midiNote && x.startSample == y.startSample
                                 && x.endSample == y.endSample; }),
           "the same seed plans the same scene, sample for sample");

    bool sorted = true, inRange = true, notesInRange = true;
    int pitchClasses = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (i > 0 && a[i].startSample < a[i - 1].startSample)
            sorted = false;
        if (a[i].endSample <= a[i].startSample || a[i].endSample > static_cast<int> (sr * 60.0))
            inRange = false;
        if (a[i].midiNote < 48 || a[i].midiNote > 75)
            notesInRange = false;
        pitchClasses |= (1 << (a[i].midiNote % 12));
    }
    check (sorted && inRange, "the plan is ordered and inside the scene");
    check (notesInRange, "the plan stays inside its requested note range");
    check (pitchClasses == (1 << 12) - 1, "a two octave plan reaches all twelve pitch classes, so it reaches every branch");

    // "density" is notes per second, so the count has to scale with it rather
    // than the plan quietly becoming a fixed-size fixture.
    const auto slow = planScene (id, sr, 30.0, 2.0);
    const auto fast = planScene (id, sr, 30.0, 20.0);
    const double slowRate = static_cast<double> (slow.size()) / 30.0;
    const double fastRate = static_cast<double> (fast.size()) / 30.0;

    std::printf ("       requested 2/s -> %.2f/s; requested 20/s -> %.2f/s\n", slowRate, fastRate);
    check (slowRate > 1.4 && slowRate < 2.8, "the requested note rate is the one that happens (low)");
    check (fastRate > 14.0 && fastRate < 28.0, "the requested note rate is the one that happens (high)");

    // A different seed is a different scene, or the soak tests one patch.
    const auto other = planScene (hash::combine (id, 7), sr, 60.0, 10.0);
    check (other.size() != a.size() || ! std::equal (a.begin(), a.end(), other.begin(),
                                                     [] (const SceneNote& x, const SceneNote& y)
                                                     { return x.midiNote == y.midiNote; }),
           "a different seed is a different scene");
}

//==============================================================================
// M3: the shared envelope (design 6.1.1).
//
// Two halves, deliberately. The first is the clock itself, driven from an
// EnvShape: repeats, one shots, the infinite duration of decision R3, and the
// fixed note off release. The second is the articulation matrix the plan asks
// for, measured off rendered audio with the section 13 metrics, because "the
// gene says X and the engine does Y" is exactly the bug class the M2 harness
// exists to catch. Every assertion here is a *direction* (attack got longer,
// the note ended itself, the hold moved), never a recorded number.
//==============================================================================
namespace
{
struct Articulation
{
    bool measured = false;
    float attackSeconds = -1.0f, holdDb = 0.0f, releaseSeconds = -1.0f;
    float endSeconds = -1.0f, peak = 0.0f;
    bool selfEnded = false;
    int notMeasured = 0;      // notesTooShort + notesSilent, for debugging a bad case
};

/// Render one note of one genome and read its articulation off the output.
Articulation articulate (const Genome& seed, uint64_t tag, int midiNote,
                        double sampleRate, double seconds, double holdSeconds)
{
    const MutationParams mp;
    PlantSnapshot plant;
    buildPlant (plant, hash::combine (hash::fromString ("envelope-articulation"), tag),
                seed, mp);

    RenderSetup setup;
    setup.plant = &plant;
    setup.sampleRate = sampleRate;
    setup.seconds = seconds;
    setup.play = offlinePlayState ();          // branch length 0: this is the seed
    setup.keepAudio = true;

    SceneNote n;
    n.midiNote = midiNote;
    n.velocity = 0.9f;
    n.startSample = 0;
    n.endSample = static_cast<int> (std::llround (holdSeconds * sampleRate / 128.0) * 128);
    setup.notes.push_back (n);

    Articulation a;
    const Rendered r = renderScene (setup);
    if (! r.m.finite || r.m.silent)
    {
        a.notMeasured = -1;
        return a;
    }

    const AudioMetrics m = measureAudio (r.left, r.right, sampleRate, setup.notes, true);
    a.notMeasured = m.notesTooShort + m.notesSilent + m.notesUnvoiced;
    if (m.notes.empty ())
        return a;

    const NoteMeasurement& nm = m.notes.front ();
    a.measured = nm.measured;
    a.attackSeconds = nm.attackSeconds;
    a.holdDb = nm.holdLevelDb;
    a.releaseSeconds = nm.releaseSeconds;
    a.endSeconds = nm.endSeconds;
    a.peak = nm.peak;
    a.selfEnded = nm.selfEnded;
    return a;
}

/// A seed at rest with a handful of genes moved, so every case below differs
/// from the instrument at rest by exactly the genes it names.
Genome seedWith (std::initializer_list<std::pair<std::size_t, float>> moves)
{
    Genome g = neutralGenome ();
    for (const auto& m : moves)
        g[m.first] = m.second;
    applyCouplings (g);
    return g;
}

/// Count the times the clock re triggered: one pass of the shape is one rise
/// followed by a clear fall away from its own maximum. The threshold is
/// relative, because a slow shape falls by a hair per sample.
int countPeaks (SharedEnv& env, int samples)
{
    int peaks = 0;
    float runMax = 0.0f;
    bool peaked = false;
    for (int i = 0; i < samples; ++i)
    {
        const float v = env.process();
        if (v > runMax)
        {
            runMax = v;
            peaked = runMax > 0.05f;
        }
        else if (peaked && v < runMax * 0.9f)
        {
            ++peaks;
            peaked = false;
            runMax = v;
        }
    }
    return peaks;
}
} // namespace

static void testSharedEnvelope()
{
    std::printf ("the shared envelope clock (6.1.1, M3)\n");

    const double sr = 48000.0;

    //------------------------------------------------------------------------
    // What the mapping layer decides, before any audio exists.
    {
        VoiceParams p;
        const BranchTraits traits;
        MacroValues macros;
        resolveVoiceParams (seedWith ({{ g_env_time, 0.95f }}), traits, macros, 60, 0.9f, p);

        const float baseAt09 = mapGene (kGenes[g_env_time], 0.9f);
        check (p.envDuration == EnvShape::kInfiniteDuration,
               "env_time >= 0.9 resolves to an infinite duration (R3)");
        check (p.envRepeats && p.envShapeSeconds < EnvShape::kInfiniteDuration * 0.5f
               && p.envShapeSeconds > 0.05f,
               "an infinite duration still has a finite shape base, never an infinite one (R3)");

        // A period longer than the fallback base is not a repeat: the shape
        // takes the base, then holds. That is the only reading in which
        // vol_fade can end a note that has no duration.
        resolveVoiceParams (seedWith ({{ g_env_time, 0.95f }, { g_env_loop, 1.0f }}),
                            traits, macros, 60, 0.9f, p);
        check (! p.envRepeats && std::abs (p.envShapeSeconds - baseAt09) < 1.0e-3f,
               "past the fallback base there are no repeats and the shape takes it");

        resolveVoiceParams (seedWith ({{ g_env_time, 0.95f }, { g_env_loop, 0.2f }}),
                            traits, macros, 60, 0.9f, p);
        check (p.envRepeats && std::abs (p.envShapeSeconds - mapGene (kGenes[g_env_loop], 0.2f)) < 1.0e-3f,
               "with a period inside it, the shape repeats at that period (endless sweep)");

        resolveVoiceParams (seedWith ({{ g_env_time, 0.5f }, { g_env_loop, 0.5f }}),
                            traits, macros, 60, 0.9f, p);
        check (! p.envRepeats,
               "env_loop equal to the duration means no repeats (4.2: longer than the duration)");

        // env_kf: above the middle, high keys shorten the note (4.2).
        const Genome kf = seedWith ({{ g_env_kf, 1.0f }});
        VoiceParams low, high;
        resolveVoiceParams (kf, traits, macros, 48, 0.9f, low);
        resolveVoiceParams (kf, traits, macros, 72, 0.9f, high);
        check (high.envDuration < low.envDuration * 0.26f && low.envDuration > high.envDuration,
               "env_kf shortens the duration on high keys and lengthens it on low ones");

        VoiceParams noneLow, noneHigh;
        resolveVoiceParams (neutralGenome (), traits, macros, 48, 0.9f, noneLow);
        resolveVoiceParams (neutralGenome (), traits, macros, 72, 0.9f, noneHigh);
        check (noneLow.envDuration == noneHigh.envDuration,
               "env_kf at neutral is exactly nothing at any key");

        // Curvature genes are exponents with the neutral value exactly linear.
        resolveVoiceParams (seedWith ({{ g_vol_atk, 0.0f }}), traits, macros, 60, 0.9f, p);
        check (std::abs (p.ampAttackExp - 0.5f) < 1.0e-4f,
               "vol_atk maps to 2^(2(v-0.5)): the bottom of the range is 0.5");
    }

    //------------------------------------------------------------------------
    // The clock itself.
    {
        SharedEnv env;
        env.setSampleRate (sr);

        // One shot, no sustain: sounds, and is over at its duration.
        EnvShape oneShot;
        oneShot.duration = 0.5f;
        oneShot.shapeSeconds = 0.5f;
        oneShot.sustain = 0.0f;
        env.start (oneShot);
        float atEnd = 1.0f, atPeak = 0.0f;
        int peakSample = -1;
        for (int i = 0; i < static_cast<int> (sr * 0.4); ++i)
        {
            const float v = env.process();
            if (v > atPeak) { atPeak = v; peakSample = i; }
        }
        const int expectPeakAt = static_cast<int> (sr * 0.25f);
        check (std::abs (peakSample - expectPeakAt) <= 2 && atPeak > 0.999f,
               "a symmetric shape peaks exactly at half its duration");
        for (int i = 0; i < static_cast<int> (sr * 0.3); ++i)
            atEnd = env.process();
        check (atEnd < 1.0e-6f && env.selfEnded() && env.finished(),
               "with no sustain the shape ends at silence and the note is over by itself");

        // Sustain holds, and the second fade takes that hold to silence.
        EnvShape held = oneShot;
        held.sustain = 0.5f;
        env.start (held);
        for (int i = 0; i < static_cast<int> (sr * 2); ++i)
            env.process();
        check (std::abs (env.ampValue() - 0.5f) < 1.0e-4f && ! env.finished(),
               "vol_sus is the floor the decay aims at, and it holds indefinitely");

        EnvShape faded = held;
        faded.fade = 1.0f;
        env.start (faded);
        for (int i = 0; i < static_cast<int> (sr * 2); ++i)
            env.process();
        check (env.ampValue() < 1.0e-5f && env.finished(),
               "vol_fade alone closes a note that has no duration of its own (R3)");

        // Repeats: one whole envelope per period, and none at all when the
        // period is not inside the duration.
        EnvShape loop;
        loop.duration = 2.0f;
        loop.repeats = true;
        loop.loopPeriod = 0.25f;
        loop.shapeSeconds = 0.25f;
        env.start (loop);
        const int peaks = countPeaks (env, static_cast<int> (sr * 2.1));

        check (peaks == 8, "a loop period inside the duration re triggers the shape that many times");

        EnvShape noLoop = loop;
        noLoop.duration = 0.2f;   // the period is past it: no repeats (4.2)
        noLoop.repeats = false;
        noLoop.shapeSeconds = 0.2f;
        env.start (noLoop);
        const int single = countPeaks (env, static_cast<int> (sr * 2));
        check (single == 1,
               "a period at or beyond the duration gives exactly one pass");

        // Note off: stop looping, fixed release from wherever the shape is.
        env.start (loop);
        for (int i = 0; i < static_cast<int> (sr * 0.05); ++i)
            env.process();
        const float levelAtOff = env.ampValue();
        env.release();
        int samplesToSilence = 0;
        while (env.sounding () && samplesToSilence < static_cast<int> (sr * 2))
        {
            env.process();
            ++samplesToSilence;
        }
        const float expected = kNoteOffReleaseSeconds * static_cast<float> (sr);
        check (std::abs (samplesToSilence - expected) <= 2.0f,
               "note off is a fixed release (D-5), sample accurate, not an env_time release");
        check (levelAtOff > 0.0f, "the release starts from the level the shape was at");

        // The mod envelope shares the clock and always returns to nothing.
        EnvShape mod;
        mod.duration = 1.0f;
        mod.shapeSeconds = 1.0f;
        mod.sustain = 1.0f;   // amp holds at full, mod must not
        env.start (mod);
        for (int i = 0; i < static_cast<int> (sr); ++i)
            env.process();
        check (std::abs (env.ampValue() - 1.0f) < 1.0e-4f && env.modValue() < 1.0e-4f,
               "the mod envelope returns to nothing on the same clock the amp sustains on");
    }
}

static void testEnvelopeArticulation()
{
    std::printf ("articulation tracks the genes (13, M3's matrix)\n");

    const double sr = 22050.0;
    bool ok = true;

    auto expect = [&ok] (bool good, const char* what)
    {
        if (! good)
        {
            ok = false;
            std::printf ("       !! failed: %s\n", what);
        }
    };

    auto show = [] (const char* name, const Articulation& a)
    {
        std::printf ("       %-22s measured %d attack %7.3f s  hold %+6.1f dB  release %6.3f s"
                     "  sound over %7.3f s  self ended %s\n",
                     name, a.measured ? 1 : 0, a.attackSeconds, a.holdDb,
                     a.releaseSeconds, a.endSeconds, a.selfEnded ? "yes" : "no");
    };

    // env_time is the length of the note: two durations, nothing else changed.
    // env_loop is parked above both so neither repeats (4.2's own rule).
    const Articulation shortNote = articulate (seedWith ({ { g_env_time, 0.30f }, { g_env_loop, 1.0f } }),
                                               1, 60, sr, 3.0, 2.4);
    const Articulation longNote = articulate (seedWith ({ { g_env_time, 0.55f }, { g_env_loop, 1.0f } }),
                                              2, 60, sr, 6.0, 5.0);
    show ("env_time short", shortNote);
    show ("env_time long", longNote);
    expect (!(! (shortNote.measured && longNote.measured)), "! (shortNote.measured && longNote.measured)");
    expect (!(! (shortNote.selfEnded && longNote.selfEnded)), "! (shortNote.selfEnded && longNote.selfEnded)");
    expect (!(! (longNote.endSeconds > shortNote.endSeconds * 1.8f)), "! (longNote.endSeconds > shortNote.endSeconds * 1.8f)");
    expect (!(! (shortNote.endSeconds > 0.2f && shortNote.endSeconds < 0.7f)), "! (shortNote.endSeconds > 0.2f && shortNote.endSeconds < 0.7f)");

    // vol_sus: below the gate there is no sustain to measure, above it there is.
    // (At neutral env_time, so the guard rail of 5.6 has no reason to intervene
    // in either case: it exists to stop a branch going *silent*, not to keep a
    // branch sustaining.)
    const Articulation noSus = articulate (seedWith ({ { g_vol_sus, 0.45f } }), 3, 60, sr, 4.0, 3.5);
    const Articulation withSus = articulate (seedWith ({ { g_vol_sus, 0.95f } }), 4, 60, sr, 4.0, 3.5);
    show ("vol_sus below gate", noSus);
    show ("vol_sus open", withSus);
    expect (!(! (noSus.measured && withSus.measured)), "! (noSus.measured && withSus.measured)");
    expect (!(! noSus.selfEnded), "! noSus.selfEnded");
    expect (!(withSus.selfEnded), "withSus.selfEnded");                       // it holds to the key up
    expect (!(! (withSus.holdDb > -12.0f && noSus.holdDb < -40.0f)), "! (withSus.holdDb > -12.0f && noSus.holdDb < -40.0f)");
    expect (!(! (withSus.releaseSeconds > 0.0f && withSus.releaseSeconds < 0.25f)), "! (withSus.releaseSeconds > 0.0f && withSus.releaseSeconds < 0.25f)");

    // Inside the dead zone it is bit identical, which is 5.7.3 restated as
    // "vol_sus < 0.5 measures no sustain".
    const Articulation otherDead = articulate (seedWith ({ { g_vol_sus, 0.30f } }), 3, 60, sr, 4.0, 3.5);
    expect (!(! (std::abs (otherDead.peak - noSus.peak) < 1.0e-6f)), "! (std::abs (otherDead.peak - noSus.peak) < 1.0e-6f)");

    // env_tilt is the attack/decay split of the shape, so it moves measured
    // attack in the documented direction and nothing else changes the timing.
    const Articulation fastAttack = articulate (seedWith ({ { g_env_time, 0.5f }, { g_env_loop, 1.0f },
                                                           { g_env_tilt, 0.1f } }), 5, 60, sr, 3.0, 2.4);
    const Articulation slowAttack = articulate (seedWith ({ { g_env_time, 0.5f }, { g_env_loop, 1.0f },
                                                           { g_env_tilt, 0.9f } }), 6, 60, sr, 3.0, 2.4);
    show ("env_tilt fast attack", fastAttack);
    show ("env_tilt slow attack", slowAttack);
    expect (!(! (fastAttack.measured && slowAttack.measured)), "! (fastAttack.measured && slowAttack.measured)");
    expect (!(! (slowAttack.attackSeconds > fastAttack.attackSeconds * 3.0f)), "! (slowAttack.attackSeconds > fastAttack.attackSeconds * 3.0f)");
    expect (!(! (fastAttack.attackSeconds > 0.05f && fastAttack.attackSeconds < 0.4f)), "! (fastAttack.attackSeconds > 0.05f && fastAttack.attackSeconds < 0.4f)");

    // vol_atk is the curvature of that same attack: front weighted versus back
    // weighted, at an unchanged split.
    const Articulation front = articulate (seedWith ({ { g_env_time, 0.5f }, { g_env_loop, 1.0f },
                                                      { g_vol_atk, 0.0f } }), 7, 60, sr, 3.0, 2.4);
    const Articulation back = articulate (seedWith ({ { g_env_time, 0.5f }, { g_env_loop, 1.0f },
                                                     { g_vol_atk, 1.0f } }), 8, 60, sr, 3.0, 2.4);
    show ("vol_atk front weighted", front);
    show ("vol_atk back weighted", back);
    expect (!(! (front.measured && back.measured)), "! (front.measured && back.measured)");
    expect (!(! (back.attackSeconds > front.attackSeconds * 1.5f)), "! (back.attackSeconds > front.attackSeconds * 1.5f)");

    // env_kf: the same plant at a low key and a high one.
    const Articulation lowKey = articulate (seedWith ({ { g_env_time, 0.45f }, { g_env_loop, 1.0f },
                                                       { g_env_kf, 1.0f } }), 9, 48, sr, 4.0, 3.5);
    const Articulation highKey = articulate (seedWith ({ { g_env_time, 0.45f }, { g_env_loop, 1.0f },
                                                        { g_env_kf, 1.0f } }), 9, 72, sr, 4.0, 3.5);
    show ("env_kf note 48", lowKey);
    show ("env_kf note 72", highKey);
    expect (!(! (lowKey.measured && highKey.measured)), "! (lowKey.measured && highKey.measured)");
    expect (!(! (highKey.endSeconds < lowKey.endSeconds * 0.6f)), "! (highKey.endSeconds < lowKey.endSeconds * 0.6f)");

    // The infinite duration (R3), rendered: the note is stopped by the key, not
    // by its shape, and it keeps sounding past what the shape's base asks for.
    const Articulation endless = articulate (seedWith ({ { g_env_time, 0.95f }, { g_env_loop, 1.0f },
                                                        { g_vol_sus, 0.9f } }), 10, 60, sr, 20.0, 18.0);
    show ("env_time >= 0.9", endless);
    expect (!(! endless.measured), "! endless.measured");
    expect (!(endless.selfEnded), "endless.selfEnded");               // still sounding when the key came up
    expect (!(! (endless.holdDb > -12.0f)), "! (endless.holdDb > -12.0f)");    // and at its sustain, not at silence
    expect (!(! (endless.releaseSeconds > 0.0f && endless.releaseSeconds < 0.25f)), "! (endless.releaseSeconds > 0.0f && endless.releaseSeconds < 0.25f)");

    // vol_fade is the documented second fade after the attack, and its
    // destination is silence: the one gene that can end a note on its own.
    const Articulation unfaded = articulate (seedWith ({ { g_env_time, 0.45f }, { g_env_loop, 1.0f },
                                                        { g_vol_sus, 0.9f } }), 11, 60, sr, 4.0, 3.5);
    const Articulation faded = articulate (seedWith ({ { g_env_time, 0.45f }, { g_env_loop, 1.0f },
                                                      { g_vol_sus, 0.9f }, { g_vol_fade, 1.0f } }),
                                          12, 60, sr, 4.0, 3.5);
    show ("no second fade", unfaded);
    show ("vol_fade at full", faded);
    expect (!(! (unfaded.measured && faded.measured)), "! (unfaded.measured && faded.measured)");
    expect (!(unfaded.selfEnded), "unfaded.selfEnded");
    expect (!(! faded.selfEnded), "! faded.selfEnded");
    expect (!(! (faded.holdDb < -40.0f && unfaded.holdDb > -12.0f)), "! (faded.holdDb < -40.0f && unfaded.holdDb > -12.0f)");

    // Below the gate (0.75) the second fade is not switched on at all, and the
    // render is bit identical to the one without it. Same tag on purpose: the
    // per voice noise/detune stream hangs off the plant's seed id, so a
    // different tag would compare two different voices rather than the gate.
    const Articulation fadeShut = articulate (seedWith ({ { g_env_time, 0.45f }, { g_env_loop, 1.0f },
                                                         { g_vol_sus, 0.9f }, { g_vol_fade, 0.6f } }),
                                             11, 60, sr, 4.0, 3.5);
    expect (!(fadeShut.selfEnded), "fadeShut.selfEnded");
    expect (!(std::abs (fadeShut.peak - unfaded.peak) > 1.0e-6f), "std::abs (fadeShut.peak - unfaded.peak) > 1.0e-6f");

    std::printf ("       durations: short %.3f s vs long %.3f s; endless shape base %.2f s\n",
                 shortNote.endSeconds, longNote.endSeconds, mapGene (kGenes[g_env_time], 0.9f));
    check (ok, "measured attack, hold, sound over and self ending move as the genes say");

    //------------------------------------------------------------------------
    // Determinism of the new clock: the same scene, twice, at three sample
    // rates, bit for bit.
    bool identical = true;
    for (const double rate : { 22050.0, 48000.0, 96000.0 })
    {
        const Genome g = seedWith ({ { g_env_time, 0.6f }, { g_env_loop, 0.3f }, { g_vol_sus, 0.8f } });
        const Articulation a = articulate (g, 14, 60, rate, 2.0, 1.5);
        const Articulation b = articulate (g, 14, 60, rate, 2.0, 1.5);
        if (! (a.measured && b.measured)) identical = false;
        if (a.peak != b.peak || a.attackSeconds != b.attackSeconds
            || a.endSeconds != b.endSeconds || a.holdDb != b.holdDb)
            identical = false;
    }
    check (identical, "the shared envelope renders bit identically when re rendered, at 3 sample rates");
}

//==============================================================================
// The M2 table (Docs/Implementation-Status.md section 12) rendered through the
// same harness. Same seed, same plant, same note events, the same *default*
// PlayState the old harness fell back to, because that is what produced those
// numbers (section 12's first finding). It is a golden of a milestone boundary
// rather than of the engine: M2 proved it changed no sample, and this table
// re-pinned at M3 is the one M4 onwards are measured against. When it moves, a
// milestone changed the audio of the instrument at rest, which is worth knowing
// in decibels rather than by ear.
//==============================================================================
static void testM2BaselineAudio()
{
    std::printf ("the M2 audio table, re-pinned after M3 (baseline for M4)\n");

    const MutationParams mp;
    const uint64_t seedId = hash::fromString ("render-seed");
    Genome seed{};
    Rng rng (seedId);
    randomizeGenome (seed, rng);

    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);

    // Recorded from the post M3 build; the rms column of the table in
    // Implementation-Status section 13, at full precision. The pre M3 column
    // (0.0704387 … 0.0721963) is kept in section 12: the shared envelope took
    // the instrument at rest down by about 8 dB, because its note now spends
    // half of env_time attacking and lands on a sustain floor instead of
    // sitting at full level. That is the design, and M7's Volume curve is the
    // milestone that re-tunes the output for it.
    static const float expectedRms[12] = {
        0.0273958f, 0.0278022f, 0.0277919f, 0.0277034f, 0.0270448f, 0.0277866f,
        0.0275757f, 0.0271672f, 0.0267898f, 0.0271518f, 0.0270851f, 0.0268318f
    };

    bool same = true;
    float worstRelative = 0.0f;

    std::printf ("       branch   note        peak          rms          dc\n");
    for (int branch = 0; branch < kNumBranches; ++branch)
    {
        RenderSetup setup;
        setup.plant = &plant;
        setup.sampleRate = 48000.0;
        setup.seconds = 2.0;
        setup.block = 128;
        // Not offlinePlayState(): deliberately the untouched default, which is
        // what the old harness effectively used (its PlayState never reached the
        // Synth). Every branch is therefore at distance 0 here.
        SceneNote n;
        n.midiNote = 60 + branch;
        n.velocity = 0.9f;
        // 8, not 0: the old call was addNoteOn (note, 0.9f, 8), i.e. a sample
        // offset of 8 slipped into the position that takes the offset. It is a
        // quirk of the old harness and nothing else, but it is part of what
        // produced the recorded numbers, so reproducing them has to keep it.
        n.startSample = 8;
        n.endSample = 12800;   // the old block * 100
        setup.notes.push_back (n);

        const Rendered r = renderScene (setup);

        const float relative = std::abs (r.m.rms - expectedRms[branch]) / expectedRms[branch];
        worstRelative = std::max (worstRelative, relative);
        if (relative > 1.0e-4f)
            same = false;

        std::printf ("       %6d %6d  %10.7f  %10.7f  %+12.9f\n",
                     branch, n.midiNote, r.m.peak, r.m.rms, r.m.dc);
    }

    check (same, "the post M3 per branch table is reproduced to 1e-4 (M3's audio is pinned)");
    std::printf ("       worst relative difference: %.2e\n", worstRelative);
}

//==============================================================================
// Design 13's batch mode: all twelve branches at five lengths, every metric.
// This is the table M3-M7 are judged against, and the first measurement of
// branch versus seed loudness this project has ever had.
//==============================================================================
namespace
{
struct BatchStats
{
    float worstDbVsSeed = 0.0f, worstDc = 0.0f, minRms = 1.0e9f, maxPeak = 0.0f;
    float worstInharmonic = 0.0f, bestFlatness = 1.0f, worstFlatness = 0.0f;
    float minCentroid = 1.0e9f, maxCentroid = 0.0f;
    float louderThanSeed = 0.0f, quieterThanSeed = 0.0f;
    int cases = 0, unusable = 0, unmeasured = 0;
    int withinSixDb = 0, overSixDb = 0;

    // Design 13's target. Named as a constant so a later milestone that meets
    // it can tighten the gate below rather than leave a loosened one behind.
    static constexpr float kTargetDbVsSeed = 6.0f;
};

void runBatch (const PlantSnapshot& plant, double sampleRate, double seconds,
               const float* lengths, int numLengths, BatchStats& stats, bool verbose)
{
    std::printf ("       br  len      peak        rms   dB/seed       dc   centroid   flat  offgrid"
                 "   attack   hold  release  self\n");

    for (int branch = 0; branch < kNumBranches; ++branch)
    {
        float seedRms = 0.0f;

        for (int li = 0; li < numLengths; ++li)
        {
            const float length = lengths[li];
            // One branch grows, the note belongs to that branch (5.3: a note
            // plays its pitch class), so this is "what does growing this branch
            // do", not "what does playing a different note do".
            const Rendered r = renderNote (plant, branch, length, 60 + branch, 0.9f,
                                           sampleRate, seconds);
            const AudioMetrics& m = r.m;

            if (li == 0)
                seedRms = m.rms;

            const float dbVsSeed = (m.rms > 0.0f && seedRms > 0.0f)
                ? 20.0f * std::log10 (m.rms / seedRms) : 999.0f;

            if (std::abs (dbVsSeed) > BatchStats::kTargetDbVsSeed)
            {
                // Name the offender rather than count it: the resolved genome and
                // the numbers the mapping layer made from it, which is the only
                // way a level regression can be read instead of guessed at.
                BranchTraits traits;
                const Genome rg = resolveBranchGenome (plant, branch, length, traits);
                VoiceParams vp;
                resolveVoiceParams (rg, traits, offlinePlayState().macros, 60 + branch, 0.9f, vp);
                std::printf ("       !! br %d len %.2f  sus %.2f fade %.2f  env %.2fs loop %.2fs rep %d"
                             " tilt %+.2f  flt %.0fHz q %.1f top %.2f mode %d sep %+.1f"
                             "  noise %.2f/%.2f form %.2f/%.2f  q %.2f sat %.2f  gain %.3f duty %.2f\n",
                             branch, length, rg[g_vol_sus], rg[g_vol_fade],
                             vp.envDuration, vp.envLoopSeconds, vp.envRepeats ? 1 : 0,
                             vp.envTilt, vp.filterHz, vp.filterQ, rg[g_flt_type],
                             vp.filterSep,
                             rg[g_a_noise], rg[g_b_noise], rg[g_a_form], rg[g_b_form],
                             rg[g_flt_q], rg[g_saturate],
                             loudnessCompensation (rg), envelopeDuty (rg));
            }

            ++stats.cases;
            bool usable = true;
            if (! m.finite || m.silent)
                usable = false;
            if (m.rms < 1.0e-4f)
                usable = false;
            if (! usable)
                ++stats.unusable;

            stats.worstDbVsSeed = std::max (stats.worstDbVsSeed, std::abs (dbVsSeed));
            stats.louderThanSeed = std::max (stats.louderThanSeed, dbVsSeed);
            stats.quieterThanSeed = std::min (stats.quieterThanSeed, dbVsSeed);
            if (std::abs (dbVsSeed) <= BatchStats::kTargetDbVsSeed)
                ++stats.withinSixDb;
            else
                ++stats.overSixDb;
            stats.worstDc = std::max (stats.worstDc, std::abs (m.dc));
            stats.minRms = std::min (stats.minRms, m.rms);
            stats.maxPeak = std::max (stats.maxPeak, m.peak);
            stats.worstInharmonic = std::max (stats.worstInharmonic, m.inharmonicRatio);
            stats.bestFlatness = std::min (stats.bestFlatness, m.flatness);
            stats.worstFlatness = std::max (stats.worstFlatness, m.flatness);
            stats.minCentroid = std::min (stats.minCentroid, m.centroidHz);
            stats.maxCentroid = std::max (stats.maxCentroid, m.centroidHz);

            if (m.notesMeasured == 0)
                ++stats.unmeasured;

            if (verbose)
            {
                std::printf ("       %2d %4.2f  %9.5f  %9.5f  %+7.2f  %9.6f  %7.0f  %5.3f  %6.3f"
                             "  %7.4f  %+5.1f  %7.4f  %s\n",
                             branch, length, m.peak, m.rms, dbVsSeed, m.dc,
                             m.centroidHz, m.flatness, m.inharmonicRatio,
                             m.attackSeconds, m.holdLevelDb, m.releaseSeconds,
                             m.selfEndedNotes > 0 ? "yes" : "-");
            }
        }
    }
}
} // namespace

static BatchStats runSeedBatch (uint64_t seedId, double sampleRate, double seconds, bool verbose)
{
    const MutationParams mp;
    Genome seed{};
    Rng rng (seedId);
    randomizeGenome (seed, rng);

    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);

    static const float lengths[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    BatchStats stats;
    if (verbose)
        std::printf ("12 branches x 5 lengths, full metrics (13 batch mode), seed %llu\n",
                     static_cast<unsigned long long> (seedId));
    runBatch (plant, sampleRate, seconds, lengths, 5, stats, verbose);

    std::printf ("       worst branch vs seed: %.2f dB   worst dc: %.5f   min rms: %.5f   "
                 "max peak: %.3f\n",
                 stats.worstDbVsSeed, stats.worstDc, stats.minRms, stats.maxPeak);
    std::printf ("       louder than seed by up to %+.2f dB, quieter by up to %.2f dB; "
                 "within 6 dB %d of %d cases\n",
                 stats.louderThanSeed, stats.quieterThanSeed, stats.withinSixDb, stats.cases);
    std::printf ("       centroid %.0f … %.0f Hz   flatness %.3f … %.3f   off grid up to %.3f   "
                 "cases %d, unusable %d, unmeasured %d\n",
                 stats.minCentroid, stats.maxCentroid, stats.bestFlatness, stats.worstFlatness,
                 stats.worstInharmonic, stats.cases, stats.unusable, stats.unmeasured);

    return stats;
}

static void testBranchBatch()
{
    const BatchStats stats = runSeedBatch (hash::fromString ("render-seed"), 48000.0, 2.0, true);

    check (stats.cases == kNumBranches * 5, "every branch and length was rendered");
    check (stats.unusable == 0, "no branch at any length is silent, NaN or inaudible");
    check (stats.unmeasured == 0, "every rendered note could be measured (the metrics work on real audio)");
    check (stats.worstDc < 0.02f, "no runaway DC anywhere in the batch");
    check (stats.maxPeak < 1.0f, "nothing clips past full scale");
    // The plan's M2 gate was "loudness relative to the seed, target ~6 dB", and
    // the M2 measurement missed it in 11 of 60 cases. M3 found the cause, and it
    // was not the envelope: it was the filterMode *trait*, which replaced the
    // filter topology outright (decision R10) and left the branches that grew
    // into "band" or "high" with a 5-20 kHz pass band where their fundamental
    // should have been - exactly the three metric signature section 12 named.
    // Removing that trait (see Genetic/Mutation.h) plus the duration aware duty
    // term in loudnessCompensation put the whole batch inside the target:
    // worst +3.64 dB, quietest -1.58 dB, 60 of 60. The gate is now the target
    // itself; the measured spread is pinned one notch above what M3 got so that
    // M5's real topologies have somewhere to fail in before the gate does.
    check (stats.overSixDb == 0, "every one of the 60 cases is inside the 6 dB target (M3's gate)");
    check (stats.worstDbVsSeed < 5.0f, "measured spread stays under 5 dB (M3 reached 3.64)");
    // Growth was level *suppressed* under M2; the duty term now lifts short and
    // percussive branches toward their seed, so a branch may come out a little
    // louder. Pin the direction and the amount anyway: +4 dB is the point where
    // M7's limiter has to be in the picture.
    check (stats.louderThanSeed < 4.0f, "growth does not make a branch more than ~4 dB louder than its seed");
    // Not a gate on taste: a branch that is pure noise or pure dullness is a
    // broken branch, which is exactly what 13 asks flatness and centroid to catch.
    check (stats.worstFlatness < 0.95f, "no branch renders as nothing but noise");
    check (stats.bestFlatness < 0.35f, "at least some branches are tonal (a field of pure noise would be a bug)");
}

//==============================================================================
// createTest / performTest at the five sample rates of the plan.
//==============================================================================
static void testGoldenProtocol()
{
    std::printf ("createTest / performTest at five sample rates (13)\n");

    static const double rates[] = { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };

    bool roundTrip = true, exact = true;

    for (double sr : rates)
    {
        TestSpec spec;
        spec.seed = 20261030;
        spec.seconds = 4.0;             // the documented run is 300 s; the gate is the round trip
        spec.sampleRate = sr;
        spec.density = 5.0;
        spec.file = tempFilePath ("genomosh_golden");
        spec.tolerance = 0.0;

        const GoldenResult made = createTest (spec);
        const GoldenResult again = performTest (spec);

        std::printf ("       sr %6.0f: frames %8zu  hash %s  maxdiff %.6f  rms %.5f -> %s\n",
                     sr, again.frames, again.hash.c_str(), again.maxSampleDiff,
                     again.m.rms, again.ok ? "ok" : (again.message.empty() ? "FAIL" : again.message.c_str()));

        if (! made.ok)
            roundTrip = false;
        if (! again.ok || again.maxSampleDiff != 0.0)
            exact = false;

        std::remove (spec.file.c_str());
        std::remove ((spec.file + ".txt").c_str());
    }

    check (roundTrip, "createTest writes a file it can read back and measure");
    check (exact, "the same spec re-renders to a bit exact file at all five sample rates");

    // The tolerance has to be a real thing: one changed sample in the file must
    // fail at 0 and pass at a tolerance larger than the change.
    {
        TestSpec spec;
        spec.seed = 20261030;
        spec.seconds = 2.0;
        spec.sampleRate = 44100.0;
        spec.density = 5.0;
        spec.file = tempFilePath ("genomosh_tolerance");
        spec.tolerance = 0.0;

        createTest (spec);

        std::FILE* f = std::fopen (spec.file.c_str(), "r+b");
        bool flipped = false;
        if (f != nullptr)
        {
            std::fseek (f, 44 + 2000, SEEK_SET);
            const int v = std::fgetc (f);
            std::fseek (f, 44 + 2000, SEEK_SET);
            std::fputc ((v ^ 0x40) & 0xff, f);
            flipped = true;
            std::fclose (f);
        }
        check (flipped, "the golden file can be edited on disk");

        const GoldenResult strict = performTest (spec);
        TestSpec loose = spec;
        loose.tolerance = 0.01;
        const GoldenResult tolerated = performTest (loose);

        std::printf ("       one byte changed in the file: max diff %.5f -> strict %s, "
                     "tolerance 0.01 %s\n",
                     strict.maxSampleDiff, strict.ok ? "passed" : "failed",
                     tolerated.ok ? "passed" : "failed");

        check (! strict.ok && strict.maxSampleDiff > 0.0, "a one sample change is caught at tolerance 0");
        check (tolerated.ok, "and forgiven at a tolerance larger than it (tolerance is honoured)");

        std::remove (spec.file.c_str());
        std::remove ((spec.file + ".txt").c_str());
    }

    // A file whose header says something other than the spec is a different
    // render, and must not be compared as if it were the same one.
    {
        TestSpec spec;
        spec.seed = 7;
        spec.seconds = 1.0;
        spec.sampleRate = 22050.0;
        spec.density = 5.0;
        spec.file = tempFilePath ("genomosh_wrongrate");
        createTest (spec);

        TestSpec other = spec;
        other.sampleRate = 44100.0;
        const GoldenResult mismatch = performTest (other);
        check (! mismatch.ok, "a golden file at the wrong sample rate is refused, not compared");

        std::remove (spec.file.c_str());
        std::remove ((spec.file + ".txt").c_str());
    }
}

//==============================================================================
// The design 13 soak. The suite runs a short version; `GenoMosh_Tests soak
// --rounds 1000` runs the gate version, and 10 000 is the reference's number.
//==============================================================================
static void testMassiveSoak (int rounds)
{
    std::printf ("massiveAudioTest, %d rounds (13)\n", rounds);

    const SoakResult s = massiveAudioTest (rounds, 1.5, false);

    std::printf ("       rounds %d: unusable %d (nan %d, silent %d, dc %d, clipped %d, over "
                 "0.995 %d), worst dc %.5f, worst peak %.4f\n",
                 s.rounds, s.unusable, s.nan, s.silent, s.dc, s.clipped, s.tooLoud,
                 s.worstDc, s.worstPeak);

    check (s.nan == 0, "the soak finds no NaN or Inf");
    check (s.dc == 0, "the soak finds no runaway DC");
    // Three rounds in a thousand summed loud enough to pass full scale (worst
    // peak 1.010): the master chain today is one unconditional soft clipper,
    // and M7 replaces it with the documented Volume curve plus a saturating
    // limiter. Named target (zero) and measured proportion (0.3%), so M7 can
    // tighten it instead of discovering it.
    check (s.clipped * 100 <= s.rounds, "no more than 1% of soak scenes reach hard clipping (0.3% at 1000 rounds today)");
    check (s.silent * 4 <= s.rounds, "at most a quarter of soak scenes are silent (the seed floor is doing its job)");
}


//==============================================================================
// What dragging a branch does to the voice pool: the UI re articulates the same
// note over and over while the pointer moves, then releases it once.
static void testNoteBookkeeping()
{
    std::printf ("note bookkeeping\n");

    const MutationParams mp;
    const uint64_t seedId = hash::fromString ("bookkeeping-seed");
    Genome seed{};
    Rng rng (seedId);
    randomizeGenome (seed, rng);

    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);

    const int block = 128;
    const int note = 64;

    Synth synth;
    synth.prepare (48000.0, block);

    PlantHandoff handoff;
    handoff.beginWrite() = plant;
    handoff.publish();
    synth.attachPlant (&handoff);

    PlayState state;
    state.macros.wheel = 1.0f;
    state.macros.wheelScale = 1.0f;
    state.macros.volume = 0.7f;
    for (int b = 0; b < kNumBranches; ++b)
        state.branchLength[static_cast<std::size_t> (b)] = 0.8f;
    synth.setPlayState (state);

    std::vector<float> bufL (static_cast<std::size_t> (block), 0.0f);
    std::vector<float> bufR (static_cast<std::size_t> (block), 0.0f);
    float* channels[2] = { bufL.data(), bufR.data() };

    int worstHeld = 0, worstVoices = 0;

    for (int i = 0; i < 200; ++i)
    {
        if (i % 4 == 0)
            synth.addNoteOn (note, 0.8f, 0);   // the pointer is still moving

        synth.render (channels, 2, block);

        worstHeld   = std::max (worstHeld, synth.numHeldNotes());
        worstVoices = std::max (worstVoices, synth.numActiveVoices());
    }

    check (worstHeld == 1, "repeating note on for one note keeps exactly one held note");
    check (worstVoices <= 1, "re articulating a held note does not spend extra voices");

    synth.addNoteOff (note, 0);
    for (int i = 0; i < 4; ++i)
        synth.render (channels, 2, block);

    check (synth.numHeldNotes() == 0, "one note off releases the note completely");

    // And a note off with no note on before it must not disturb anything.
    synth.addNoteOff (note, 0);
    synth.render (channels, 2, block);
    check (synth.numHeldNotes() == 0, "a stray note off is harmless");
}

//==============================================================================
//==============================================================================
// Command line: the vocabulary of design 13, so a run can be written down in a
// status document and repeated by anyone.
//
//   GenoMosh_Tests                                   the suite (what ctest runs)
//   GenoMosh_Tests createTest  --seed N --time S --sr H --density D --file F
//   GenoMosh_Tests performTest -- same --tolerance T
//   GenoMosh_Tests batch       [--seed N] [--sr H] [--time S]
//   GenoMosh_Tests soak        [--rounds N]
//   GenoMosh_Tests noiseBank   --out DIR [--time S]
//
// The suite runs the short versions of the protocol (a few seconds of audio per
// rate); the documented golden set is the same calls with the document's times.
//==============================================================================
namespace
{
struct Args
{
    std::vector<std::pair<std::string, std::string>> values;

    bool has (const std::string& name) const
    {
        for (const auto& v : values)
            if (v.first == name)
                return true;
        return false;
    }
    std::string str (const std::string& name, const std::string& fallback = "") const
    {
        for (const auto& v : values)
            if (v.first == name)
                return v.second;
        return fallback;
    }
    double number (const std::string& name, double fallback) const
    {
        if (! has (name))
            return fallback;
        return std::atof (str (name).c_str());
    }
};

TestSpec specFromArgs (const Args& args)
{
    TestSpec spec;
    spec.seed = static_cast<uint64_t> (std::strtoull (args.str ("seed", "1").c_str(), nullptr, 10));
    spec.seconds = args.number ("time", 300.0);
    spec.sampleRate = args.number ("sr", 44100.0);
    spec.density = args.number ("density", 10.0);
    spec.file = args.str ("file");
    spec.tolerance = args.number ("tolerance", 0.001);
    spec.branchLength = static_cast<float> (args.number ("length", 0.6));
    return spec;
}

void printGolden (const char* command, const TestSpec& spec, const GoldenResult& r)
{
    std::printf ("%s seed %llu time %g sr %g density %g file %s\n",
                 command, static_cast<unsigned long long> (spec.seed), spec.seconds,
                 spec.sampleRate, spec.density, spec.file.c_str());
    std::printf ("       frames %zu hash %s maxdiff %.6f (max %d lsb) rmsdiff %.8f\n",
                 r.frames, r.hash.c_str(), r.maxSampleDiff, r.maxDiffLsb, r.rmsDiff);
    std::printf ("       measured file: peak %.5f rms %.5f dc %+.5f centroid %.0f Hz "
                 "flatness %.4f off grid %.4f\n",
                 r.m.peak, r.m.rms, r.m.dc, r.m.centroidHz, r.m.flatness, r.m.inharmonicRatio);
    std::printf ("       %s\n", r.ok ? "PASS" : ("FAIL: " + r.message));
}
} // namespace

static void runSuite()
{
    const MutationParams mp;

    // Measured once (Docs/Rev3-Alignment-Plan.md M0's gate): the genome is 50
    // floats and a PlantSnapshot carries 36 of them, across a triple buffer.
    // PlantView no longer copies one per timer tick, but the number is still
    // worth watching at every milestone that adds data.
    std::printf ("GenoMosh core tests (%d genes: %d documented + %d extras, %d branches)\n",
                 static_cast<int> (kNumGenes),
                 static_cast<int> (kNumCoreGenes),
                 static_cast<int> (kNumGenes - kNumCoreGenes), kNumBranches);
    std::printf ("       sizeof Genome = %zu B, BranchDirection = %zu B, "
                 "PlantSnapshot = %zu B, triple buffer = %zu B\n",
                 sizeof (Genome), sizeof (BranchDirection), sizeof (PlantSnapshot),
                 sizeof (PlantSnapshot) * 3);
    testTableMatchesDoc();
    testCurves();
    testMappingNeutralIsRest();
    testActivationGates();
    testRailsAndMixWarp();
    testGrowthExempt();
    testRatioSnapping();
    testSeedsClusterAroundNeutral();
    testPullToNeutral();
    testAtonalityCeiling();
    testPitchStability();
    testGenome (mp);
    testFilter();

    // M2, the section 13 harness: the tooling is tested before anything measured
    // with it is believed.
    testWavFileIsAFile();
    testMetricsOnKnownSignals();
    testScenePlanIsSeeded();
    testGoldenProtocol();

    // And the audio, through that tooling.
    testNeutralGateIsAudible();
    testSharedEnvelope();
    testEnvelopeArticulation();
    testM2BaselineAudio();
    testBranchBatch();
    testMassiveSoak (40);
    testNoteBookkeeping();
}

int main (int argc, char** argv)
{
    Args args;
    std::string command;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind ("--", 0) == 0)
        {
            const std::string name = a.substr (2);
            std::string value = "1";
            if (i + 1 < argc && argv[i + 1][0] != '-')
            {
                value = argv[i + 1];
                ++i;
            }
            args.values.push_back ({ name, value });
        }
        else if (command.empty())
            command = a;
    }

    if (command.empty())
    {
        runSuite();
        std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "FAILURES", failures);
        return failures == 0 ? 0 : 1;
    }

    if (command == "createTest" || command == "performTest")
    {
        const TestSpec spec = specFromArgs (args);
        if (spec.file.empty())
        {
            std::printf ("%s needs --file <path>\n", command.c_str());
            return 2;
        }
        const GoldenResult r = command == "createTest" ? createTest (spec) : performTest (spec);
        printGolden (command.c_str(), spec, r);
        return r.ok ? 0 : 1;
    }

    if (command == "batch")
    {
        const uint64_t seedId = args.has ("seed")
            ? static_cast<uint64_t> (std::strtoull (args.str ("seed").c_str(), nullptr, 10))
            : hash::fromString ("render-seed");
        runSeedBatch (seedId, args.number ("sr", 48000.0), args.number ("time", 2.0), true);
        return 0;
    }

    if (command == "soak")
    {
        testMassiveSoak (static_cast<int> (args.number ("rounds", 1000.0)));
        std::printf ("\n%s (%d failures)\n", failures == 0 ? "SOAK CLEAN" : "SOAK FAILURES", failures);
        return failures == 0 ? 0 : 1;
    }

    if (command == "noiseBank")
    {
        // design 13's generateRandomNoises: 100 files, density (i/10)^2 + 1.
        const std::string dir = args.str ("out", "/tmp/genomosh-noisebank");
        const double seconds = args.number ("time", 4.0);
        ::mkdir (dir.c_str(), 0777);

        for (int i = 0; i < 100; ++i)
        {
            TestSpec spec;
            spec.seed = hash::combine (hash::fromString ("noiseBank"),
                                       static_cast<uint64_t> (i));
            spec.seconds = seconds;
            spec.sampleRate = args.number ("sr", 44100.0);
            spec.density = (static_cast<double> (i) / 10.0) * (static_cast<double> (i) / 10.0) + 1.0;

            char name[32];
            std::snprintf (name, sizeof (name), "/noise_%03d.wav", i);
            spec.file = dir + name;

            const GoldenResult r = createTest (spec);
            std::printf ("noise %3d seed %llu density %6.2f -> %s  rms %.4f centroid %.0f "
                         "flatness %.3f\n",
                         i, static_cast<unsigned long long> (spec.seed), spec.density,
                         r.ok ? "ok" : (r.message.empty() ? "fail" : r.message.c_str()),
                         r.m.rms, r.m.centroidHz, r.m.flatness);
        }
        return 0;
    }

    std::printf ("unknown command '%s'\n", command.c_str());
    return 2;
}
