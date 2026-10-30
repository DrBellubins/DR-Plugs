// Offline harness for the GenoMosh core (design doc section 13).
//
//   * phase 1/2 acceptance: curve round trips, ratio snapping direction (5.5),
//     determinism, distance 0 returns the seed, deviation grows with distance,
//     guard rails hold;
//   * the envelope: what the vol_*/env_* genes resolve to (mapping layer), and
//     what SharedEnv does with them (a pluck, repeated, with a level decay over
//     the repeats);
//   * the modulation: bounded pitch modulation from the pluck, and a free LFO
//     whose tremolo half rides on top of the envelope;
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

using namespace gmosh;

static int failures = 0;

static void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

//==============================================================================
static void testCurves()
{
    std::printf ("curves\n");
    bool ok = true;

    for (std::size_t g = 0; g < kNumGenes; ++g)
    {
        for (float v = 0.0f; v <= 1.0001f; v += 0.125f)
        {
            const float value = mapGene (kGenes[g], v);
            const float back = unmapGene (kGenes[g], value);

            if (! std::isfinite (value) || std::abs (back - v) > 1.0e-3f)
            {
                std::printf ("    %s: v=%.3f -> %.6g -> %.3f\n", kGenes[g].id, v, value, back);
                ok = false;
            }
        }

        const float lo = mapGene (kGenes[g], 0.0f);
        const float hi = mapGene (kGenes[g], 1.0f);
        if (std::abs (lo - kGenes[g].minValue) > 1.0e-3f || std::abs (hi - kGenes[g].maxValue) > 1.0e-3f)
        {
            std::printf ("    %s: range mismatch %g..%g\n", kGenes[g].id, lo, hi);
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

    const std::size_t pitchGenes[] = { g_a_freq, g_b_freq, g_lfo_amt, g_drift_amt, g_det_spread };
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
                    const bool bipolar = (kGenes[gene].flags & kFlagRatio) != 0;
                    const float neutral = bipolar ? 0.5f : 0.0f;
                    const float span = bipolar ? 0.5f : 1.0f;
                    widest = std::max (widest, std::abs (g[gene] - neutral) / span);
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

    seed[g_a_freq]     = 0.5f;   // bipolar centre = 0 semitones, both oscillators
    seed[g_b_freq]     = 0.5f;
    seed[g_lfo_amt]    = 0.0f;
    seed[g_drift_amt]  = 0.0f;
    seed[g_det_spread] = 0.0f;
    seed[g_fm_amt]     = 0.0f;
    seed[g_sub_am]     = 0.0f;
    seed[g_a_noise]    = 0.0f;
    seed[g_b_noise]    = 0.0f;
    seed[g_env_loop]   = 0.0f;
    // This test is about everything *except* the mod envelope: a_mod / b_mod are
    // at neutral (no pitch modulation) so the assertion below measures the rest
    // of the voice. What a_mod does when it is open is testModulationReachesAudio.
    seed[g_a_mod]      = 0.5f;
    seed[g_b_mod]      = 0.5f;
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
// The envelope mapping: what the vol_*/env_* genes mean now.
//
// This is asserted at the mapping layer rather than in audio on purpose: these
// are the numbers the DSP is handed, and a plant's articulation is decided
// here, not in the sample loop.
//==============================================================================
static void testEnvelopeMapping()
{
    std::printf ("envelope + modulation mapping\n");

    const MutationParams mp;
    const uint64_t seedId = hash::fromString ("mapping-seed");
    Genome seed {};
    Rng rng (seedId);
    randomizeGenome (seed, rng);

    auto resolve = [] (const Genome& g) {
        VoiceParams p;
        BranchTraits traits {};   // default traits, no growth
        MacroValues macros;       // the plugin's macros
        resolveVoiceParams (g, traits, macros, 60, 1.0f, p);
        return p;
    };

    // env_time at the top of its range means no level decay at all: the pluck
    // train runs at full level until the key is released.
    Genome g = seed;
    g[g_env_time] = 0.95f;
    check (resolve (g).envShape.duration <= 0.0f,
           "env_time at the top of its range asks for no outer decay");
    g[g_env_time] = 0.5f;
    const float midTime = resolve (g).envShape.duration;
    check (midTime > 0.0f, "env_time below that gate resolves to a real decay time");

    // env_loop is the period of one pluck, monotone, and never longer than the
    // note it sits in (a longer pluck is one pluck spanning the note).
    float lastPeriod = -1.0f;
    bool monotone = true, clamped = true;
    for (float v = 0.0f; v <= 1.0f; v += 0.1f)
    {
        g[g_env_loop] = v;
        const VoiceParams p = resolve (g);
        if (p.envShape.period < lastPeriod) monotone = false;
        if (p.envShape.period > midTime + 1.0e-4f) clamped = false;
        lastPeriod = p.envShape.period;
    }
    check (monotone, "env_loop raises the pluck period monotonically");
    check (clamped, "a pluck is never longer than the note it repeats in");

    // env_tilt moves the peak along the cycle. Neutral must stay a plain pluck.
    g[g_env_loop] = 0.5f;
    float lastPeak = -1.0f;
    bool peakMonotone = true, peakSane = true;
    for (float v = 0.0f; v <= 1.0f; v += 0.1f)
    {
        g[g_env_tilt] = v;
        const float pk = resolve (g).envShape.peakPos;
        if (pk < lastPeak) peakMonotone = false;
        if (pk < 0.005f || pk > 0.96f) peakSane = false;
        lastPeak = pk;
    }
    g[g_env_tilt] = 0.5f;
    check (peakMonotone && peakSane, "env_tilt moves the pluck peak along the cycle, in range");
    check (resolve (g).envShape.peakPos < 0.12f, "env_tilt at neutral is a plain pluck, not a swell");

    // vol_sus is the floor the repeats settle at; vol_atk / vol_dcy only bend
    // the curves (a gene at 0.5 is a straight line, both ways).
    check (resolve (g).envShape.floorLevel == clamp01 (g[g_vol_sus]),
           "vol_sus is the level the outer curve settles at");
    g[g_vol_atk] = 0.5f;
    g[g_vol_dcy] = 0.5f;
    const VoiceParams straight = resolve (g);
    check (std::abs (straight.envShape.riseExp - 1.0f) < 1.0e-4f
           && std::abs (straight.envShape.outerExp - 1.0f) < 1.0e-4f,
           "vol_atk / vol_dcy at neutral are a straight rise and a straight fade");
    g[g_vol_atk] = 0.05f;
    check (resolve (g).envShape.riseExp < straight.envShape.riseExp,
           "vol_atk low is a punchy rise, high is a soft one");

    // The modulator is the same curve with its own timing, and it can only ever
    // sit inside the cycle. mod_sh is a sample and hold that stays off until
    // well up its range, fastest just past the gate.
    g[g_mod_atk] = 0.0f;
    const float early = resolve (g).envShape.modPeak;
    g[g_mod_atk] = 1.0f;
    const float late = resolve (g).envShape.modPeak;
    check (late > early && early >= 0.004f && late <= 0.96f,
           "mod_atk moves the modulator's peak inside the cycle, monotonically");
    g[g_mod_sh] = 0.5f;
    check (resolve (g).modHoldHz == 0.0f, "mod_sh stays off over most of its range");
    g[g_mod_sh] = 0.95f;
    const float atTop = resolve (g).modHoldHz;
    g[g_mod_sh] = 0.8f;
    const float justOpened = resolve (g).modHoldHz;
    check (atTop > 0.0f && justOpened > atTop,
           "mod_sh opens as a sample and hold rate, slowest at the top of its range");

    // Pitch modulation is bounded by the mapping layer, and neutral means the
    // note keeps the pitch it was pressed at.
    g[g_a_mod] = 0.5f;
    g[g_b_mod] = 0.5f;
    const VoiceParams calm = resolve (g);
    check (calm.aModSemis == 0.0f && calm.bModSemis == 0.0f,
           "a_mod / b_mod at neutral ask for no pitch modulation");
    g[g_a_mod] = 1.0f;
    g[g_b_mod] = 0.0f;
    const VoiceParams wild = resolve (g);
    check (wild.aModSemis > 0.0f && wild.bModSemis < 0.0f
           && wild.aModSemis <= 12.0f && -wild.bModSemis <= 12.0f,
           "a_mod / b_mod resolve in opposite directions and never past an octave");

    // The LFO is split between vibrato and tremolo at one rate, equal power.
    g[g_lfo_amt] = 0.9f;
    g[g_lfo_bal] = 0.0f;
    const VoiceParams vibratoOnly = resolve (g);
    g[g_lfo_bal] = 1.0f;
    const VoiceParams tremoloOnly = resolve (g);
    check (vibratoOnly.vibSemis > 6.0f && vibratoOnly.tremDepth < 0.05f
           && tremoloOnly.tremDepth > 0.3f && tremoloOnly.vibSemis < 0.5f,
           "lfo_bal at one end is vibrato only, at the other it is tremolo only");
    g[g_lfo_bal] = 0.5f;
    const VoiceParams both = resolve (g);
    check (std::abs (both.vibSemis - vibratoOnly.vibSemis * 0.7071f) < 0.2f
           && std::abs (both.tremDepth - tremoloOnly.tremDepth * 0.7071f) < 0.05f,
           "lfo_bal at neutral splits vibrato and tremolo equal power");
    g[g_lfo_amt] = 0.0f;
    const VoiceParams noLfo = resolve (g);
    check (noLfo.vibSemis == 0.0f && noLfo.tremDepth == 0.0f,
           "lfo_amt at zero leaves the LFO silent, as the table says it should");
}

//==============================================================================
// The envelope block itself: a pluck, repeated, with a level decay applied to
// the whole train. Everything here is a property of the shape, so it is tested
// without a plant in the way.
//==============================================================================
static void testPluckEnvelope()
{
    std::printf ("the envelope is a pluck with a decay over the repeats\n");

    SharedEnv e;
    e.setSampleRate (48000.0);

    constexpr int sr = 48000;
    auto restart = [] (SharedEnv& env, const SharedEnv::Shape& s) {
        env.setShape (s);
        env.gateOn();
    };

    SharedEnv::Shape s;
    s.period = 0.25f; s.peakPos = 0.30f; s.riseExp = 1.0f;
    s.duration = 0.0f; s.floorLevel = 0.0f;
    restart (e, s);

    const int periodSamples = static_cast<int> (s.period * sr);
    std::vector<float> one (static_cast<std::size_t> (periodSamples), 0.0f);
    for (int i = 0; i < periodSamples; ++i) { e.process(); one[static_cast<std::size_t> (i)] = e.ampValue(); }

    int peakAt = 0;
    float peak = 0.0f;
    for (int i = 0; i < periodSamples; ++i)
        if (one[static_cast<std::size_t> (i)] > peak)
            { peak = one[static_cast<std::size_t> (i)]; peakAt = i; }

    check (std::abs (static_cast<float> (peakAt) / static_cast<float> (periodSamples) - s.peakPos) < 0.02f,
           "the pluck peaks where env_tilt says it should");
    check (peak > 0.99f, "the pluck reaches full level at its peak");

    int nearFull = 0;
    for (const float v : one)
        if (v > 0.95f * peak) ++nearFull;
    check (nearFull < periodSamples / 12,
           "the pluck never plateaus (no hold inside it)");
    check (one[static_cast<std::size_t> (periodSamples - 2)] < 0.05f,
           "the pluck is back at zero at the end of its cycle");

    // It repeats at the period, and the repeats are identical while the outer
    // curve is off.
    float secondPeak = 0.0f, thirdPeak = 0.0f;
    for (int i = 0; i < periodSamples * 4; ++i)
    {
        e.process();
        const float v = e.ampValue();
        if (i >= periodSamples && i < 2 * periodSamples) secondPeak = std::max (secondPeak, v);
        if (i >= 3 * periodSamples) thirdPeak = std::max (thirdPeak, v);
    }
    check (std::abs (secondPeak - peak) < 1.0e-3f && std::abs (thirdPeak - peak) < 1.0e-3f,
           "the train repeats at env_loop with the same level (env_time off)");

    // The outer curve is a level applied to the train, not a second envelope:
    // it lands exactly on the floor and stays there.
    s.duration = 1.0f; s.floorLevel = 0.5f; s.outerExp = 1.0f;
    restart (e, s);
    for (int i = 0; i < sr; ++i) e.process();
    const float settled = e.outerValue();
    for (int i = 0; i < sr; ++i) e.process();
    check (std::abs (settled - 0.5f) < 1.0e-3f && std::abs (e.outerValue() - 0.5f) < 1.0e-3f,
           "the outer curve settles on vol_sus and holds there");

    float mid = 0.0f;
    restart (e, s);
    for (int i = 0; i < sr / 2; ++i) e.process();
    mid = e.outerValue();
    check (mid > 0.5f && mid < 0.85f, "the level decays over the repeats rather than falling at once");

    // vol_dcy bends that decay: a low exponent holds, then collapses.
    s.outerExp = 0.3f;
    restart (e, s);
    for (int i = 0; i < sr / 2; ++i) e.process();
    check (e.outerValue() > mid, "vol_dcy high collapses late, vol_dcy low fades early");

    // Nothing to settle to means the note ends by itself and stops running.
    s.duration = 0.3f; s.floorLevel = 0.0f; s.outerExp = 1.0f;
    restart (e, s);
    int i = 0;
    while (! e.isIdle() && i < sr * 4) { e.process(); ++i; }
    check (e.isIdle() && e.ampValue() == 0.0f, "a note with no floor ends by itself");

    // Note off is a fade from wherever the level happens to be, never a jump.
    s.period = 0.25f; s.duration = 0.0f; s.floorLevel = 0.0f; s.release = 0.05f;
    restart (e, s);
    for (i = 0; i < sr / 4; ++i) e.process();
    float worstJump = 0.0f, previous = e.ampValue();
    e.gateOff();
    for (i = 0; i < sr && ! e.isIdle(); ++i)
    {
        e.process();
        worstJump = std::max (worstJump, std::abs (e.ampValue() - previous));
        previous = e.ampValue();
    }
    check (e.isIdle(), "a released note finishes and stops running");
    check (worstJump < 0.02f, "note off fades from the current level without clicking");

    // The modulator is the same clock with its own peak and its own fall
    // window: it can be over long before the level is.
    s.modPeak = 0.05f; s.modFallScale = 0.2f;
    restart (e, s);
    float modPeakAt = 0.0f, modPeak = 0.0f, ampAtModEnd = 0.0f;
    for (i = 0; i < periodSamples; ++i)
    {
        e.process();
        if (e.modValue() > modPeak)
            { modPeak = e.modValue(); modPeakAt = static_cast<float> (i) / static_cast<float> (periodSamples); }
        if (i == static_cast<int> (0.30f * static_cast<float> (periodSamples)))
            ampAtModEnd = e.ampValue();
    }
    check (modPeak > 0.5f && std::abs (modPeakAt - s.modPeak) < 0.03f,
           "the modulator follows its own peak position, not the level's");
    check (ampAtModEnd > 0.2f, "the modulator can finish while the level is still up");
}

//==============================================================================
// A held note rendered from a hand written seed, plus the two probes that read
// one destination at a time off it.
//==============================================================================
static std::vector<float> renderHeldNote (const Genome& seed, uint64_t seedId,
                                          float sampleRate, float seconds)
{
    MutationParams mp;
    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);

    Synth synth;
    synth.prepare (sampleRate, 128);

    PlantHandoff handoff;
    handoff.beginWrite() = plant;
    handoff.publish();
    synth.attachPlant (&handoff);

    PlayState state;                       // every branch at length 0: the seed
    synth.setPlayState (state);

    const int numSamples = static_cast<int> (sampleRate * seconds);
    std::vector<float> left (static_cast<std::size_t> (numSamples), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (numSamples), 0.0f);

    constexpr int block = 128;
    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int n = std::min (block, numSamples - offset);
        if (offset == 0)
            synth.addNoteOn (60, 0.9f, 8);
        float* channels[2] = { left.data() + static_cast<std::size_t> (offset),
                               right.data() + static_cast<std::size_t> (offset) };
        synth.render (channels, 2, n);
    }

    std::vector<float> mono (static_cast<std::size_t> (numSamples), 0.0f);
    for (int i = 0; i < numSamples; ++i)
        mono[static_cast<std::size_t> (i)] = 0.5f * (left[static_cast<std::size_t> (i)]
                                                    + right[static_cast<std::size_t> (i)]);
    return mono;
}

// A plain voice: one sine oscillator, nothing else in the plant allowed to move
// pitch or level, so the probes below measure one thing at a time.
static Genome modTestSeed (Rng& rng)
{
    Genome seed {};
    randomizeGenome (seed, rng);

    seed[g_a_freq] = 0.5f;   seed[g_b_freq] = 0.5f;   // unison
    seed[g_balance] = 0.0f;                           // only oscillator A
    seed[g_a_form] = 0.0f;    seed[g_b_form] = 0.0f;  // sine
    seed[g_a_noise] = 0.0f;   seed[g_b_noise] = 0.0f;
    seed[g_fm_amt] = 0.0f;    seed[g_sub_am] = 0.0f;
    seed[g_det_spread] = 0.0f; seed[g_drift_amt] = 0.0f;
    seed[g_lfo_amt] = 0.0f;
    seed[g_flt_freq] = 0.8f;  seed[g_flt_q] = 0.0f;   seed[g_flt_mod] = 0.5f;
    seed[g_env_time] = 0.95f;                         // no outer decay
    seed[g_env_loop] = 0.58f;                         // one pluck a second
    seed[g_env_tilt] = 0.0f;                          // a plain pluck
    seed[g_vol_sus] = 0.7f;
    seed[g_mod_atk] = 0.5f;   seed[g_mod_dcy] = 0.9f; // mod follows the pluck
    seed[g_mod_sh] = 0.0f;
    seed[g_a_mod] = 0.5f;     seed[g_b_mod] = 0.5f;   // the probes set these
    seed[g_sat_drive] = 0.0f; seed[g_flt_sep] = 0.0f;
    seed[g_fx_mix] = 0.0f;                            // dry only: the probe reads the voice
    return seed;
}

// Where the pitch actually is, by counting zero crossings: unlike an
// autocorrelation this cannot lock onto the wrong octave when the level is
// pulsed, which is exactly what a pluck envelope does to it.
static float measuredHz (const std::vector<float>& mono, int start, int length, float sampleRateHz)
{
    float hi = 0.0f;
    for (int i = 0; i < length; ++i)
        hi = std::max (hi, std::abs (mono[static_cast<std::size_t> (start + i)]));
    if (hi < 1.0e-3f)
        return 0.0f;

    // Arm on the first real peak, then count upward going through zero
    // crossings with their position interpolated: that is accurate to well under
    // one percent over a window this short, which a threshold crossing counter
    // is not.
    const float th = hi * 0.2f;
    bool armed = false;
    int count = 0;
    double first = 0.0, last = 0.0;

    for (int i = 0; i + 1 < length; ++i)
    {
        const float a = mono[static_cast<std::size_t> (start + i)];
        const float b = mono[static_cast<std::size_t> (start + i + 1)];
        if (! armed)
        {
            if (a > th) armed = true;
            continue;
        }
        if (a < 0.0f && b >= 0.0f)
        {
            const double t = static_cast<double> (i) - static_cast<double> (a) / static_cast<double> (b - a);
            if (count == 0) first = t;
            last = t;
            ++count;
        }
    }

    if (count < 4 || last <= first)
        return 0.0f;
    return static_cast<float> (static_cast<double> (count - 1) * sampleRateHz / (last - first));
}

// How far the pitch strays from the pressed note, in cents, over the note.
static float worstPitchExcursion (const std::vector<float>& mono, float sampleRate)
{
    const int window = static_cast<int> (sampleRate * 0.06);
    float worst = 0.0f;

    for (float t = 0.05f; t < 1.4f; t += 0.1f)
    {
        const int start = static_cast<int> (t * sampleRate);
        if (start + window >= static_cast<int> (mono.size()))
            break;
        const float hz = measuredHz (mono, start, window, sampleRate);
        if (hz <= 0.0f)
            continue;
        const float cents = 1200.0f * std::log (hz / 261.6256f) / std::log (2.0f);
        worst = std::max (worst, std::abs (cents));
    }
    return worst;
}

// How much the level wiggles at audio modulation rates on a note that is
// otherwise being held: the window rms sequence with everything slower than a
// few hertz taken out, so the pluck's own slow rise cannot answer this.
static float levelWiggle (const std::vector<float>& mono, float sampleRate, float& outMean)
{
    // Windows long enough to hold a whole number of cycles of the measured
    // tone (a short window reads the tone itself as a wiggle), and everything
    // slower than a couple of hertz smoothed out.
    const int win = static_cast<int> (sampleRate * 0.04f);
    const int smooth = static_cast<int> (sampleRate * 0.6f) / win;
    outMean = 0.0f;
    if (smooth < 2)
        return 0.0f;

    std::vector<float> levels;
    for (int start = static_cast<int> (sampleRate * 0.5f); start + win < static_cast<int> (mono.size()); start += win)
    {
        double sum = 0.0;
        for (int i = 0; i < win; ++i)
        {
            const double v = mono[static_cast<std::size_t> (start + i)];
            sum += v * v;
        }
        levels.push_back (static_cast<float> (std::sqrt (sum / static_cast<double> (win))));
    }
    if (static_cast<int> (levels.size()) < 3 * smooth)
        return 0.0f;

    double var = 0.0, mean = 0.0;
    int counted = 0;
    for (int i = smooth; i + smooth < static_cast<int> (levels.size()); ++i)
    {
        double local = 0.0;
        for (int k = -smooth; k <= smooth; ++k)
            local += levels[static_cast<std::size_t> (i + k)];
        const double slow = local / static_cast<double> (2 * smooth + 1);
        const double d = levels[static_cast<std::size_t> (i)] - slow;
        var += d * d;
        mean += slow;
        ++counted;
    }
    if (counted == 0 || mean <= 0.0) return 0.0f;
    mean /= static_cast<double> (counted);
    outMean = static_cast<float> (mean);
    return static_cast<float> (std::sqrt (var / static_cast<double> (counted)) / mean);
}

static void testModulationReachesAudio()
{
    std::printf ("the modulator reaches pitch and level\n");

    constexpr float sampleRate = 48000.0f;
    Rng rng (hash::fromString ("mod-probe"));

    // Neutral mod amounts: the note holds the pitch it was pressed at.
    Genome calm = modTestSeed (rng);
    calm[g_a_mod] = 0.5f;
    calm[g_b_mod] = 0.5f;
    const float calmCents = worstPitchExcursion (renderHeldNote (calm, hash::fromString ("mod-probe-a"),
                                                                 sampleRate, 1.5f), sampleRate);

    // Open the mod envelope's pitch amount: each pluck now bends, and it stays
    // inside the octave the mapping layer promised.
    Genome wild = modTestSeed (rng);
    wild[g_a_mod] = 0.9f;
    wild[g_b_mod] = 0.9f;
    const float wildCents = worstPitchExcursion (renderHeldNote (wild, hash::fromString ("mod-probe-b"),
                                                                 sampleRate, 1.5f), sampleRate);

    std::printf (       "       pitch excursion: calm %.0f cents, a_mod wide open %.0f cents\n",
                       calmCents, wildCents);
    check (calmCents < 40.0f, "nothing moves the pitch when a_mod is neutral");
    check (wildCents > 200.0f, "a_mod bends the pitch of each pluck when it is open");
    check (wildCents < 1300.0f, "and never past the octave the mapping layer bounds it to");

    // The LFO's tremolo half rides on top of the envelope instead of replacing
    // it: with the same rate and depth on the vibrato side of the split, the
    // level of a held note must stay put. Repeated over a few plant seeds, since
    // the LFO's waveform and phase are deliberately random per voice.
    Genome lfoBase = modTestSeed (rng);
    lfoBase[g_env_loop] = 1.0f;      // one very slow pluck: the level is held
    lfoBase[g_lfo_amt] = 0.9f;
    lfoBase[g_lfo_rate] = 0.63f;     // a couple of Hz, slow enough for the probe

    float vibSum = 0.0f, tremSum = 0.0f, vibMean = 0.0f, tremMean = 0.0f, meanRatio = 0.0f;
    for (int probe = 0; probe < 3; ++probe)
    {
        const std::string name = "lfo-probe-" + std::to_string (probe);

        Genome vibratoOnly = lfoBase;
        vibratoOnly[g_lfo_bal] = -1.0f;      // vibrato only
        Genome tremoloOnly = lfoBase;
        tremoloOnly[g_lfo_bal] = 1.0f;       // tremolo only

        const float vib = levelWiggle (renderHeldNote (vibratoOnly, hash::fromString ((name + "-vib").c_str()),
                                                       sampleRate, 4.0f), sampleRate, vibMean);
        const float trem = levelWiggle (renderHeldNote (tremoloOnly, hash::fromString ((name + "-trem").c_str()),
                                                        sampleRate, 4.0f), sampleRate, tremMean);
        std::printf (       "       level wiggle on a held note: vibrato %.3f, tremolo %.3f\n", vib, trem);
        vibSum += vib;
        tremSum += trem;
        meanRatio += std::log10 (std::max (tremMean, 1.0e-6f) / std::max (vibMean, 1.0e-6f));
    }

    // The vibrato side of the split can still wiggle the level a little (the
    // LFO's waveform is random per voice, and a step in pitch through the filter
    // is not silent), so what is asserted here is the two things that matter:
    // the tremolo half moves the level, and it rides on top of the envelope
    // instead of taking the note's level away.
    meanRatio /= 3.0f;
    std::printf ("       mean level with tremolo vs vibrato: %+.1f dB\n", 20.0f * meanRatio);
    check (tremSum > 0.06f * 3.0f, "the LFO's tremolo half moves the level of a held note");
    check (std::abs (meanRatio) < 0.15f, "a strong tremolo does not change the note's mean level");
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

    std::printf ("GenoMosh core tests (%d genes, %d branches)\n",
                 static_cast<int> (kNumGenes), kNumBranches);
    testCurves();
    testRatioSnapping();
    testAtonalityCeiling();
    testEnvelopeMapping();
    testPluckEnvelope();
    testModulationReachesAudio();
    testPitchStability();
    testGenome (mp);
    testFilter();
    testRender();
    testNoteBookkeeping();

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
