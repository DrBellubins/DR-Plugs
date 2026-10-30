// Offline harness for the GenoMosh core (design doc section 13).
//
//   * phase 1/2 acceptance: curve round trips, ratio snapping direction (5.5),
//     determinism, distance 0 returns the seed, deviation grows with distance,
//     guard rails hold;
//   * a numeric check of the trapezoidal SVF against a reference biquad;
//   * an offline render of every branch at several distances with level,
//     silence, NaN and DC checks, plus a bit identical reproducibility test.
//
// Build: see Tests/CMakeLists.txt, or directly
//   g++ -std=c++17 -O2 Genetic/*.cpp Engine/*.cpp Tests/EngineTests.cpp -o /tmp/gmosh_tests -lm

#include "Engine/Synth.h"
#include "Genetic/Curves.h"
#include "Genetic/PlantEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
    bool ok = true;

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
    check (usable > total * 3 / 4, "majority of far branches pass the safety score");
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

//==============================================================================
struct RenderResult
{
    float peak = 0.0f, rms = 0.0f, dc = 0.0f;
    bool finite = true;
};

static RenderResult renderBranch (const PlantSnapshot& plant, const MutationParams& /*mp*/,
                                  int branch, float distance, int note, float sampleRate)
{
    const int block = 128;
    const int seconds = 2;
    const int numSamples = static_cast<int> (sampleRate) * seconds;

    Synth synth;
    synth.prepare (sampleRate, block);

    PlantHandoff handoff;
    auto& target = handoff.beginWrite();
    target = plant;
    handoff.publish();
    synth.attachPlant (&handoff);

    PlayState state;
    state.macros.wheel = 1.0f;
    state.macros.wheelScale = 1.0f;
    state.macros.volume = 0.7f;
    for (int b = 0; b < kNumBranches; ++b)
        state.branchLength[static_cast<std::size_t> (b)] = (b == branch) ? distance : 0.0f;

    std::vector<float> bufferL (static_cast<std::size_t> (numSamples), 0.0f);
    std::vector<float> bufferR (static_cast<std::size_t> (numSamples), 0.0f);
    float* channels[2] = { bufferL.data(), bufferR.data() };

    RenderResult result;
    double sumSquares = 0.0, sum = 0.0;

    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int n = std::min (block, numSamples - offset);
        if (offset == 0)
            synth.addNoteOn (note, 0.9f, 8);
        if (offset == block * 100)
            synth.addNoteOff (note, 0);

        // The engine renders one block starting at channel[0][0], exactly like
        // a host hands a block of the bus to the plugin.
        float* blockChannels[2] = { bufferL.data() + static_cast<std::size_t> (offset),
                                   bufferR.data() + static_cast<std::size_t> (offset) };
        synth.render (blockChannels, 2, n);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = bufferL[static_cast<std::size_t> (i)];
        const float r = bufferR[static_cast<std::size_t> (i)];

        if (! std::isfinite (l) || ! std::isfinite (r))
            result.finite = false;

        result.peak = std::max (result.peak, std::abs (l));
        result.peak = std::max (result.peak, std::abs (r));
        sumSquares += static_cast<double> (l) * l + static_cast<double> (r) * r;
        sum += static_cast<double> (l) + r;
    }

    result.rms = static_cast<float> (std::sqrt (sumSquares / (2.0 * numSamples)));
    result.dc = static_cast<float> (sum / (2.0 * numSamples));
    return result;
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
// b_freq, lfo_amt, drift_amt, det_spread): at 0 the plant cannot touch pitch at
// all, at 1 the whole range is available, and in between nothing may sit further
// from its neutral point than the ceiling allows. It is applied to the seed, to
// the branch directions and to the resolved genome, so it has to be idempotent:
// planting a branch feeds a gated genome back in as the next seed.
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

static void testRender()
{
    std::printf ("offline render\n");

    const MutationParams mp;
    const uint64_t seedId = hash::fromString ("render-seed");
    Genome seed{};
    Rng rng (seedId);
    randomizeGenome (seed, rng);

    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);

    const float sampleRate = 48000.0f;
    bool allFinite = true, allAudible = true, levelsOk = true;
    float referenceRms = 0.0f;

    std::printf ("       branch  dist    peak     rms      dc\n");
    for (int branch = 0; branch < kNumBranches; ++branch)
    {
        const RenderResult r = renderBranch (plant, mp, branch, 0.85f, 60 + branch, sampleRate);
        std::printf ("       %6d  %.2f  %6.3f  %6.4f  %+6.4f\n",
                     branch, 0.85, r.peak, r.rms, r.dc);

        allFinite = allFinite && r.finite;
        if (! r.finite || r.rms < 1.0e-4f)
            allAudible = false;
        if (std::abs (r.dc) > 0.05f)
            levelsOk = false;

        if (branch == 0)
            referenceRms = r.rms;
        else if (referenceRms > 0.0f && r.rms > 0.0f)
        {
            const float db = 20.0f * std::log10 (r.rms / referenceRms);
            if (std::abs (db) > 12.0f)
                levelsOk = false;
        }
    }

    check (allFinite, "no NaN or Inf in any rendered branch");
    check (allAudible, "every branch makes an audible sound");
    check (levelsOk, "no runaway DC and branches stay within ~12 dB of each other");

    // Reproducibility: same inputs, bit identical output.
    const RenderResult a = renderBranch (plant, mp, 3, 0.6f, 63, sampleRate);
    const RenderResult b = renderBranch (plant, mp, 3, 0.6f, 63, sampleRate);
    check (a.peak == b.peak && a.rms == b.rms, "rendering is bit identical across runs");

    // Sample rate independence.
    for (float sr : { 44100.0f, 96000.0f })
    {
        const RenderResult r = renderBranch (plant, mp, 5, 0.7f, 65, sr);
        check (r.finite && r.rms > 1.0e-4f, "renders cleanly at a different sample rate");
    }
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
int main()
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
    testGrowthExempt();
    testRatioSnapping();
    testAtonalityCeiling();
    testPitchStability();
    testGenome (mp);
    testFilter();
    testRender();
    testNoteBookkeeping();

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
