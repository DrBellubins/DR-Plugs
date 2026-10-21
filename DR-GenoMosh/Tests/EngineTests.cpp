// Offline harness for the GenoMosh core (design doc section 13).
//
//   * phase 1/2 acceptance: curve round trips, determinism, distance 0 returns
//     the seed, deviation grows with distance, guard rails hold;
//   * a numeric check of the trapezoidal SVF against a reference biquad;
//   * an offline render of every branch at several distances with level,
//     silence, NaN and DC checks, plus a bit identical reproducibility test.
//
// Build: see Tests/CMakeLists.txt, or directly
//   g++ -std=c++17 -O2 Genetic/*.cpp Engine/*.cpp Tests/EngineTests.cpp -o /tmp/gmosh_tests -lm

#include "Engine/Synth.h"
#include "Genetic/Curves.h"
#include "Genetic/PlantEngine.h"

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
        for (std::size_t g = 0; g < kNumGenes; ++g)
            if (g0[g] != seed[g])
                zeroIdentity = false;
    }
    check (zeroIdentity, "distance 0 returns the seed exactly");

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
int main()
{
    const MutationParams mp;

    std::printf ("GenoMosh core tests (%d genes, %d branches)\n",
                 static_cast<int> (kNumGenes), kNumBranches);
    testCurves();
    testGenome (mp);
    testFilter();
    testRender();

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL TESTS PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
