// The design doc section 13 protocol, implemented (plan milestone M2).
// See Tests/Render.h for what belongs here and why.

#include "Render.h"

#include "Genetic/Rng.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gmosh
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

int previousPowerOfTwo (int v)
{
    int p = 1;
    while (p * 2 <= v)
        p *= 2;
    return p;
}

//==============================================================================
// A plain iterative radix-2 FFT. The harness has no dependency on an FFT
// library on purpose (Render.h): the numbers it prints are the ones a status
// document quotes, so they have to come from code we also control. This is not
// on the audio thread and it is not bit critical - it is double precision with
// a recurrence for the twiddles, which is far more accurate than anything the
// metrics do with the result.
//==============================================================================
void fft (std::vector<double>& re, std::vector<double>& im, bool inverse)
{
    const std::size_t n = re.size();

    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }

    const double sign = inverse ? 1.0 : -1.0;

    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = sign * 2.0 * kPi / static_cast<double> (len);
        const double wr = std::cos (ang), wi = std::sin (ang);

        for (std::size_t i = 0; i < n; i += len)
        {
            double cr = 1.0, ci = 0.0;
            for (std::size_t k = 0; k < (len >> 1); ++k)
            {
                const std::size_t a = i + k;
                const std::size_t b = a + (len >> 1);

                const double tr = cr * re[b] - ci * im[b];
                const double ti = cr * im[b] + ci * re[b];

                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;

                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }

    if (inverse)
    {
        const double inv = 1.0 / static_cast<double> (n);
        for (std::size_t i = 0; i < n; ++i)
        {
            re[i] *= inv;
            im[i] *= inv;
        }
    }
}

/// Analysis window length: as long as the sample rate is comfortable with
/// (~85 ms) but never longer than the shortest note in the scene, because a
/// window that spills over a key change measures two notes at once. A long
/// single note (the batch) therefore gets better frequency resolution than a
/// dense scene does, which is the right way round.
std::size_t analysisFftSize (double sampleRate, double shortestNoteSeconds)
{
    const int byRate = static_cast<int> (sampleRate * 0.09);
    const int byNote = shortestNoteSeconds > 0.0 ? static_cast<int> (shortestNoteSeconds * sampleRate * 0.7)
                                                : byRate;
    const int wanted = previousPowerOfTwo (std::max (64, std::min (byRate, byNote)));
    return static_cast<std::size_t> (std::max (512, std::min (8192, wanted)));
}

struct Spectrum
{
    std::vector<double> power;   // bins 0..n/2
    std::vector<double> mag;     // |X| for the same bins, for the grid search
    std::size_t fftSize = 0;     // the transform length these bins came from
    double binHz = 0.0;
};

/// Linear interpolation of the magnitude at an arbitrary frequency. The harmonic
/// grid is not on bin centers.
double magAt (const Spectrum& s, double hz)
{
    const double k = hz / s.binHz;
    const std::size_t k0 = static_cast<std::size_t> (k);
    if (k0 + 1 >= s.mag.size())
        return 0.0;
    const double frac = k - static_cast<double> (k0);
    return s.mag[k0] * (1.0 - frac) + s.mag[k0 + 1] * frac;
}

void applyHann (std::vector<double>& re, const std::vector<float>& src, std::size_t offset)
{
    const std::size_t n = re.size();
    for (std::size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i)
                                               / static_cast<double> (n));
        re[i] = static_cast<double> (src[offset + i]) * w;
    }
}

Spectrum analyse (std::vector<double>& re, std::vector<double>& im, double sampleRate)
{
    const std::size_t n = re.size();
    fft (re, im, false);

    Spectrum s;
    s.fftSize = n;
    s.binHz = sampleRate / static_cast<double> (n);
    s.power.resize (n / 2);
    s.mag.resize (n / 2);
    for (std::size_t k = 0; k < n / 2; ++k)
    {
        s.power[k] = re[k] * re[k] + im[k] * im[k];
        s.mag[k] = std::sqrt (s.power[k]);
    }
    return s;
}

/// Autocorrelation of the same windowed frame, from the same spectrum
/// (Wiener-Khinchine), which is why measuring a note costs one FFT and not two.
void autocorrelate (const Spectrum& s, std::vector<double>& r)
{
    const std::size_t n = s.fftSize;
    std::vector<double> re (n), im (n, 0.0);
    for (std::size_t k = 0; k < s.power.size(); ++k)
        re[k] = s.power[k];
    fft (re, im, true);
    r.assign (n, 0.0);
    for (std::size_t k = 0; k < n; ++k)
        r[k] = re[k];
}

/// The fundamental of a frame: the spacing of the harmonic grid the energy is
/// actually on. Autocorrelation rather than peak picking, because the played
/// pitch is not the requested one (a_freq steps the pitch around and b_freq
/// offsets by semitones), and the aliasing estimate has to be built on the grid
/// that is sounding, not the one the note number implies.
///
/// Two stages, both validated against known signals in EngineTests:
///
///  1. Seed, from the autocorrelation: not the largest peak (that is an
///     octave-low trap for anything harmonic rich, where the correlation at
///     half the period is almost as strong), but the *first* local maximum that
///     comes close to the largest. A repeating frame correlates at its period
///     first.
///  2. Refine, on the spectrum: search grid spacings over +-15% around the seed
///     and keep the one whose harmonics capture the most magnitude. The seed's
///     octave is kept, its spacing is corrected, which is what an
///     autocorrelation alone gets a few percent wrong when an inharmonic
///     component pulls the correlation peak sideways.
///
/// What it does not do: decide between a fundamental and a subharmonic of it. A
/// grid half as coarse explains exactly the same energy, and the estimate stays
/// on the seed's octave. For an off grid *ratio* that is the conservative
/// error (a too fine grid calls everything harmonic), and it is deterministic,
/// which is what a milestone to milestone comparison needs.
double estimateF0 (const Spectrum& spec, const std::vector<double>& r, double sampleRate)
{
    const std::size_t n = r.size();
    const std::size_t lagMin = std::max<std::size_t> (4, static_cast<std::size_t> (sampleRate / 4000.0));
    const std::size_t lagMax = std::min (n / 2, static_cast<std::size_t> (sampleRate / 25.0));

    if (lagMax <= lagMin || r[0] <= 1.0e-12)
        return 0.0;

    double bestScore = -1.0;
    for (std::size_t lag = lagMin; lag <= lagMax; ++lag)
        bestScore = std::max (bestScore, r[lag] / r[0]);

    // Below this the "pitch" is a coincidence rather than a periodicity: noise
    // gets no harmonic grid, and the caller counts it as unvoiced instead of
    // letting the sieve declare all of it inharmonic.
    if (bestScore < 0.15)
        return 0.0;

    std::size_t seedLag = 0;
    for (std::size_t lag = lagMin; lag <= lagMax; ++lag)
    {
        const double score = r[lag] / r[0];
        if (score >= 0.9 * bestScore && score > r[lag - 1] / r[0] && score >= r[lag + 1] / r[0])
        {
            seedLag = lag;
            break;
        }
    }
    if (seedLag == 0)
        return 0.0;

    const double seedHz = sampleRate / static_cast<double> (seedLag);
    const double nyq = sampleRate * 0.5;

    auto captures = [&] (double f0)
    {
        const int harmonics = std::min (16, static_cast<int> (0.9 * nyq / f0));
        double sum = 0.0;
        for (int m = 1; m <= harmonics; ++m)
            sum += magAt (spec, f0 * static_cast<double> (m));
        return sum;
    };

    double best = -1.0;
    std::vector<std::pair<double, double>> grid;
    for (double f = seedHz * 0.85; f <= seedHz * 1.20; f *= 1.001)
    {
        const double s = captures (f);
        grid.push_back ({ f, s });
        best = std::max (best, s);
    }

    // The midpoint of the near-best plateau, rather than its first point: a
    // pure tone has a wide plateau centred on itself, and taking the first
    // point that reaches it biases the answer a few Hz high.
    double first = 0.0, last = 0.0;
    for (const auto& g : grid)
    {
        if (g.second >= 0.999 * best)
        {
            if (first <= 0.0)
                first = g.first;
            last = g.first;
        }
    }

    return first > 0.0 ? 0.5 * (first + last) : seedHz;
}

struct SpectralBits
{
    double centroidHz = 0.0;
    double flatness = 0.0;
    double inharmonic = 0.0;
    bool voiced = false;
};

SpectralBits spectralOf (const Spectrum& s, double sampleRate, double f0)
{
    SpectralBits out;

    const std::size_t kMin = std::max<std::size_t> (1, static_cast<std::size_t> (25.0 / s.binHz));
    const std::size_t kMax = s.power.size() - 1;

    double sumP = 0.0, sumFP = 0.0, sumLog = 0.0, sumM = 0.0;
    double peakBin = 0.0;
    for (std::size_t k = kMin; k <= kMax; ++k)
        peakBin = std::max (peakBin, s.power[k]);

    const double magFloor = std::sqrt (std::max (peakBin, 1.0e-30)) * 1.0e-6;

    for (std::size_t k = kMin; k <= kMax; ++k)
    {
        const double p = s.power[k];
        const double f = static_cast<double> (k) * s.binHz;
        const double m = std::sqrt (p);

        sumP += p;
        sumFP += f * p;
        sumLog += std::log (m + magFloor);
        sumM += m;
    }

    if (sumP <= 0.0 || sumM <= 0.0)
        return out;

    out.centroidHz = static_cast<float> (sumFP / sumP);

    // Spectral flatness: geometric mean over arithmetic mean of the magnitudes.
    // 1 is noise like, 0 is a single tone. Design 13 wants it "to catch
    // all-noise or all-dull branches", which is exactly what this separates.
    const double geo = std::exp (sumLog / static_cast<double> (kMax - kMin + 1));
    out.flatness = geo / (sumM / static_cast<double> (kMax - kMin + 1));

    // The aliasing estimate, design 13's "energy above the expected harmonic
    // series". A band limited source has energy on the harmonic grid of its
    // fundamental and nowhere else; anything that aliased back from above
    // Nyquist lands off that grid, and so does noise. So this is reported as an
    // off grid energy fraction per branch and compared between milestones, not
    // asserted as an absolute: a branch with a_noise up high is meant to score
    // high here. What it catches is the cheap digital sound M6 exists to fix.
    if (f0 > 0.0)
    {
        out.voiced = true;
        const double tol = std::max (2.5 * s.binHz, 0.02 * f0 + s.binHz);
        double offGrid = 0.0;
        for (std::size_t k = kMin; k <= kMax; ++k)
        {
            const double f = static_cast<double> (k) * s.binHz;
            const double nearest = std::round (f / f0) * f0;
            if (std::abs (f - nearest) > tol)
                offGrid += s.power[k];
        }
        out.inharmonic = offGrid / sumP;
    }

    return out;
}

//==============================================================================
// Amplitude envelope, in 1 ms blocks, of the energy across both channels.
// Mono summing is the wrong thing here: branch pan is deliberately spread, so a
// summed envelope can cancel a note that is plainly audible.
//==============================================================================
struct Envelope
{
    std::vector<float> env;
    int hop = 1;
    double sampleRate = 48000.0;

    int indexAt (int sample) const { return sample / hop; }
    float at (int i) const { return i >= 0 && i < static_cast<int> (env.size()) ? env[i] : 0.0f; }
    double secondsAt (int i) const { return static_cast<double> (i) * static_cast<double> (hop) / sampleRate; }
};

Envelope makeEnvelope (const std::vector<float>& left, const std::vector<float>& right,
                       double sampleRate)
{
    Envelope e;
    e.hop = std::max (16, static_cast<int> (sampleRate * 0.001));
    e.sampleRate = sampleRate;

    const std::size_t num = left.size();
    const std::size_t blocks = num / static_cast<std::size_t> (e.hop) + 1;
    e.env.resize (blocks, 0.0f);

    for (std::size_t b = 0; b < blocks; ++b)
    {
        const std::size_t start = b * static_cast<std::size_t> (e.hop);
        const std::size_t end = std::min (num, start + static_cast<std::size_t> (e.hop));
        double sum = 0.0;
        for (std::size_t i = start; i < end; ++i)
            sum += static_cast<double> (left[i]) * left[i]
                 + static_cast<double> (right[i]) * right[i];
        e.env[b] = static_cast<float> (std::sqrt (sum / static_cast<double> (2 * (end - start))));
    }

    return e;
}

void measureOneNote (const std::vector<float>& left, const std::vector<float>& right,
                     const SceneNote& note, double sampleRate, std::size_t nfft,
                     const Envelope& env, float scenePeak,
                     NoteMeasurement& out)
{
    out.midiNote = note.midiNote;

    const int hold = note.endSample - note.startSample;
    const int sceneEnd = static_cast<int> (left.size());
    const int frameStartMax = note.startSample + hold - static_cast<int> (nfft);

    if (hold < static_cast<int> (nfft) + hold / 4 || frameStartMax < note.startSample)
        return;   // too short for the window: reported as notesTooShort, not guessed at

    // Analyse the note where it is loudest inside the key hold: for a one shot
    // that is near the attack, for a sustaining note near the end, and the
    // metric should describe the sound the branch actually makes.
    std::size_t frame = static_cast<std::size_t> (note.startSample);
    {
        double best = -1.0;
        const double span = static_cast<double> (frameStartMax - note.startSample);
        const double fracs[] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
        for (double f : fracs)
        {
            const int at = note.startSample + static_cast<int> (span * f);
            const std::size_t from = static_cast<std::size_t> (std::max (note.startSample, at));
            double energy = 0.0;
            for (std::size_t i = 0; i < nfft; i += 16)
                energy += static_cast<double> (left[from + i]) * left[from + i];
            if (energy > best)
            {
                best = energy;
                frame = from;
            }
        }
    }

    std::vector<double> re (nfft), im (nfft, 0.0);
    applyHann (re, left, frame);
    Spectrum spec = analyse (re, im, sampleRate);

    std::vector<double> ac;
    autocorrelate (spec, ac);
    const double f0 = estimateF0 (spec, ac, sampleRate);

    const SpectralBits sb = spectralOf (spec, sampleRate, f0);

    out.f0Hz = static_cast<float> (f0);
    out.centroidHz = static_cast<float> (sb.centroidHz);
    out.flatness = static_cast<float> (sb.flatness);
    out.inharmonicRatio = sb.voiced ? static_cast<float> (sb.inharmonic) : 1.0f;

    // Level of the note on its own, and the articulation read off the envelope.
    const int iStart = env.indexAt (note.startSample);
    const int iEnd = env.indexAt (std::min (note.endSample, sceneEnd));
    const int iTail = env.indexAt (std::min (sceneEnd, note.endSample + static_cast<int> (sampleRate * 2.0)));

    float peak = 0.0f;
    for (int i = iStart; i <= iEnd; ++i)
        peak = std::max (peak, env.at (i));

    if (peak < 1.0e-5f || peak < 0.01f * scenePeak)
        return;   // the note made no sound; counted as notesSilent by the caller

    out.peak = peak;

    const float onThreshold = 0.005f * peak;
    int onset = -1;
    for (int i = iStart; i <= iEnd; ++i)
        if (env.at (i) > onThreshold) { onset = i; break; }

    float endLevel = 0.0f;
    for (int i = onset >= 0 ? onset : iStart; i <= iTail; ++i)
        endLevel = std::max (endLevel, env.at (i));

    // The sound falls silent when it drops below -40 dB of its own peak, and
    // stays silent (which is the only way to tell "released" from "gated").
    //
    // It has to have been loud first. Searching from the onset, which is defined
    // at half a percent of the peak, reported a slow attack as "over" a few
    // blocks after it began: an envelope that takes seconds to reach its peak is
    // under 1% of it for longer than the confirmation window, and M3 made such
    // attacks a thing the genes ask for (env_time is the length of the note).
    const float offThreshold = 0.01f * peak;
    const int confirmBlocks = std::max (4, static_cast<int> (sampleRate * 0.05) / env.hop);
    int iSilent = -1;
    bool sounded = false;
    for (int i = (onset >= 0 ? onset : iStart); i <= iTail; ++i)
    {
        if (! sounded)
        {
            if (env.at (i) >= offThreshold)
                sounded = true;
            continue;
        }

        if (env.at (i) < offThreshold)
        {
            bool stayedDown = true;
            for (int j = i; j < std::min (iTail, i + confirmBlocks); ++j)
                if (env.at (j) >= offThreshold) { stayedDown = false; break; }
            if (stayedDown) { iSilent = i; break; }
        }
    }

    if (onset >= 0)
    {
        int atPeak = -1;
        for (int i = onset; i <= iEnd; ++i)
            if (env.at (i) >= 0.9f * peak) { atPeak = i; break; }

        if (atPeak >= 0)
            out.attackSeconds = static_cast<float> (env.secondsAt (atPeak) - env.secondsAt (onset));
        else
            out.reachedPeak = false;
    }

    // Sustain: the highest level in the last 150 ms before the key comes up.
    // Taking the maximum rather than the mean is deliberate - tremolo and the
    // shared envelope both wobble the hold, and M3 wants to know how loud the
    // held part is, not how it wobbles.
    const int holdFrom = std::max (iStart, env.indexAt (note.endSample)
                                   - static_cast<int> (sampleRate * 0.15) / env.hop);
    float holdLevel = 0.0f;
    for (int i = holdFrom; i < iEnd; ++i)
        holdLevel = std::max (holdLevel, env.at (i));
    out.holdLevelDb = holdLevel > 1.0e-6f ? 20.0f * std::log10 (holdLevel / peak) : -80.0f;

    out.sustainSeconds = static_cast<float> (static_cast<double> (note.endSample - note.startSample)
                                            / sampleRate);

    if (iSilent >= 0)
    {
        out.endSeconds = static_cast<float> (env.secondsAt (iSilent) - env.secondsAt (iStart));
        out.selfEnded = iSilent < env.indexAt (note.endSample);
        out.releaseSeconds = out.selfEnded
            ? 0.0f
            : std::max (0.0f, static_cast<float> (env.secondsAt (iSilent)
                                                  - env.secondsAt (env.indexAt (note.endSample))));
    }
    else
    {
        out.endSeconds = -1.0f;       // still sounding at the end of the measured window
        out.releaseSeconds = -1.0f;   // the tail outlives the scene
    }

    out.measured = true;
}
} // namespace

//==============================================================================
// Scene plan
//==============================================================================
std::vector<SceneNote> planScene (uint64_t seedId, double sampleRate, double seconds,
                                  double notesPerSecond, int noteLo, int noteHi,
                                  int maxSimultaneous)
{
    std::vector<SceneNote> notes;

    if (sampleRate <= 0.0 || seconds <= 0.0 || notesPerSecond <= 0.0 || noteHi < noteLo)
        return notes;

    Rng rng (hash::combine (seedId, hash::fromString ("scene-notes")));

    const double meanGap = 1.0 / notesPerSecond;
    const double endOfScene = seconds - 0.02;

    std::vector<double> activeEnds;   // keys still down, to bound the voice count
    double t = 0.05;

    while (t < endOfScene)
    {
        // Drop the keys that have come up before this one goes down.
        activeEnds.erase (std::remove_if (activeEnds.begin(), activeEnds.end(),
                                          [=] (double e) { return e <= t; }),
                          activeEnds.end());

        const int note = noteLo + rng.integer (noteHi - noteLo + 1);
        const float velocity = rng.range (0.45f, 1.0f);

        // A gap with the requested mean. Jittered rather than regular so the
        // scene is not a metronome, which would make the articulation metric
        // measure the clock instead of the envelope.
        const double gap = meanGap * (0.35 + 1.3 * static_cast<double> (rng.uniform ()));

        // Holds scale with the gap, so "density" means note rate and not
        // polyphony. A tenth of the notes are long, which is what puts
        // sustained parts into a golden file.
        double hold = gap * (0.35 + 1.35 * static_cast<double> (rng.range (0.0f, 1.0f)));
        if (rng.chance (0.1f))
            hold *= 4.0;
        hold = std::max (0.04, hold);

        if (static_cast<int> (activeEnds.size()) >= maxSimultaneous)
            hold = std::min (hold, gap * 0.4);

        SceneNote n;
        n.midiNote = note;
        n.velocity = velocity;
        n.startSample = static_cast<int> (std::llround (t * sampleRate));
        n.endSample = static_cast<int> (std::llround (std::min (t + hold, endOfScene) * sampleRate));

        if (n.endSample > n.startSample)
        {
            notes.push_back (n);
            activeEnds.push_back (t + hold);
        }

        t += gap;
    }

    std::sort (notes.begin(), notes.end(), [] (const SceneNote& a, const SceneNote& b)
               { return a.startSample < b.startSample; });

    return notes;
}

//==============================================================================
// Measurement
//==============================================================================
float AudioMetrics::dbVs (const AudioMetrics& other) const
{
    if (rms <= 0.0f || other.rms <= 0.0f)
        return 999.0f;
    return 20.0f * std::log10 (rms / other.rms);
}

AudioMetrics measureAudio (const std::vector<float>& left, const std::vector<float>& right,
                           double sampleRate, const std::vector<SceneNote>& notes,
                           bool keepPerNote)
{
    AudioMetrics m;
    m.notesPlanned = static_cast<int> (notes.size());

    const std::size_t num = left.size();
    if (num == 0 || left.size() != right.size())
    {
        m.finite = false;
        m.silent = true;
        return m;
    }

    double sumSq = 0.0, sum = 0.0;
    for (std::size_t i = 0; i < num; ++i)
    {
        const float l = left[i], r = right[i];
        if (! std::isfinite (l) || ! std::isfinite (r))
            m.finite = false;

        m.peak = std::max (m.peak, std::max (std::abs (l), std::abs (r)));
        sumSq += static_cast<double> (l) * l + static_cast<double> (r) * r;
        sum += static_cast<double> (l) + r;
    }
    m.rms = static_cast<float> (std::sqrt (sumSq / static_cast<double> (2 * num)));
    m.dc = static_cast<float> (sum / static_cast<double> (2 * num));
    m.silent = m.peak < AudioMetrics::kSilencePeak || ! m.finite;

    if (! m.finite || m.silent)
        return m;

    // With no note plan (measuring a file on disk, say) the whole thing is one
    // note: the spectral half of the metrics still works, articulation does not.
    std::vector<SceneNote> plan = notes;
    if (plan.empty())
    {
        SceneNote whole;
        whole.startSample = 0;
        whole.endSample = static_cast<int> (num);
        plan.push_back (whole);
    }

    double shortestNote = 0.0;
    for (const SceneNote& n : plan)
    {
        const double hold = static_cast<double> (n.endSample - n.startSample) / sampleRate;
        if (n.endSample > n.startSample && (shortestNote <= 0.0 || hold < shortestNote))
            shortestNote = hold;
    }

    const std::size_t nfft = analysisFftSize (sampleRate, shortestNote);
    const Envelope env = makeEnvelope (left, right, sampleRate);

    double sumCentroid = 0.0, sumFlat = 0.0, sumInharmonic = 0.0;
    double sumAttack = 0.0, sumHold = 0.0, sumRelease = 0.0, sumEnd = 0.0;
    int attackCount = 0, releaseCount = 0, endCount = 0, voicedCount = 0;

    for (const SceneNote& n : plan)
    {
        if (n.startSample < 0 || n.endSample > static_cast<int> (num) || n.endSample <= n.startSample)
            continue;

        NoteMeasurement nm;
        measureOneNote (left, right, n, sampleRate, nfft, env, m.peak, nm);

        if (! nm.measured)
        {
            const int hold = n.endSample - n.startSample;
            if (hold < static_cast<int> (nfft) + hold / 4)
                ++m.notesTooShort;
            else
                ++m.notesSilent;
        }
        else
        {
            ++m.notesMeasured;
            m.loudestNotePeak = std::max (m.loudestNotePeak, nm.peak);
            sumCentroid += nm.centroidHz;
            sumFlat += nm.flatness;

            // A note with no detectable fundamental has no harmonic grid to be
            // off, so it is counted separately instead of being averaged in as
            // "all of it is inharmonic".
            if (nm.f0Hz > 0.0f)
            {
                sumInharmonic += nm.inharmonicRatio;
                ++voicedCount;
            }
            else
                ++m.notesUnvoiced;

            if (nm.attackSeconds >= 0.0f)
            {
                sumAttack += nm.attackSeconds;
                ++attackCount;
            }
            sumHold += nm.holdLevelDb;
            if (nm.releaseSeconds >= 0.0f)
            {
                sumRelease += nm.releaseSeconds;
                ++releaseCount;
            }
            if (nm.endSeconds >= 0.0f)
            {
                sumEnd += nm.endSeconds;
                ++endCount;
            }
            if (nm.selfEnded)
                ++m.selfEndedNotes;
        }

        if (keepPerNote)
            m.notes.push_back (nm);
    }

    if (m.notesMeasured > 0)
    {
        const double k = static_cast<double> (m.notesMeasured);
        m.centroidHz = static_cast<float> (sumCentroid / k);
        m.flatness = static_cast<float> (sumFlat / k);
        m.holdLevelDb = static_cast<float> (sumHold / k);
        m.inharmonicRatio = voicedCount > 0
            ? static_cast<float> (sumInharmonic / static_cast<double> (voicedCount)) : 0.0f;
        m.attackSeconds = attackCount > 0 ? static_cast<float> (sumAttack / attackCount) : -1.0f;
        m.releaseSeconds = releaseCount > 0 ? static_cast<float> (sumRelease / releaseCount) : -1.0f;
        m.soundEndSeconds = endCount > 0 ? static_cast<float> (sumEnd / endCount) : -1.0f;
    }

    return m;
}

//==============================================================================
// Render
//==============================================================================
PlayState offlinePlayState()
{
    PlayState p;
    p.macros.wheel = 1.0f;
    p.macros.wheelScale = 1.0f;
    return p;
}

PlayState offlinePlayState (int branch, float branchLength)
{
    PlayState p = offlinePlayState();
    if (branch >= 0 && branch < kNumBranches)
        p.branchLength[static_cast<std::size_t> (branch)] = branchLength;
    return p;
}

PlantSnapshot seededPlant (uint64_t seedId, const MutationParams& mp)
{
    Genome seed{};
    Rng rng (hash::combine (seedId, hash::fromString ("seed-genome")));
    randomizeGenome (seed, rng);

    PlantSnapshot plant;
    buildPlant (plant, seedId, seed, mp);
    return plant;
}

Rendered renderScene (const RenderSetup& setup)
{
    Rendered out;

    const int numSamples = std::max (0, static_cast<int> (std::llround (setup.seconds * setup.sampleRate)));
    const int block = setup.block > 0 ? setup.block : 128;

    if (setup.plant == nullptr || numSamples == 0)
    {
        out.m.finite = false;
        return out;
    }

    Synth synth;
    synth.prepare (setup.sampleRate, block);

    PlantHandoff handoff;
    auto& target = handoff.beginWrite();
    target = *setup.plant;
    handoff.publish();
    synth.attachPlant (&handoff);

    // Explicit on purpose. The first version of this harness built a PlayState,
    // filled it in, and never handed it to the Synth, so every "per branch"
    // number in the M0 and M1 status sections was measured with every branch at
    // distance 0 and the plugin's default macros - the seed played at twelve
    // different notes, not twelve grown branches. (Docs/Implementation-Status.md
    // section 12.) Nothing may depend on what the engine's copy of a PlayState
    // happens to start out as.
    synth.setPlayState (setup.play);

    std::vector<float> left (static_cast<std::size_t> (numSamples), 0.0f);
    std::vector<float> right (static_cast<std::size_t> (numSamples), 0.0f);

    std::vector<SceneNote> notes = setup.notes;
    std::sort (notes.begin(), notes.end(), [] (const SceneNote& a, const SceneNote& b)
               { return a.startSample < b.startSample; });

    std::size_t nextOn = 0, nextOff = 0;

    for (int offset = 0; offset < numSamples; offset += block)
    {
        const int n = std::min (block, numSamples - offset);
        const int blockEnd = offset + n;

        while (nextOn < notes.size() && notes[nextOn].startSample < blockEnd)
        {
            synth.addNoteOn (notes[nextOn].midiNote, notes[nextOn].velocity,
                             notes[nextOn].startSample - offset);
            ++nextOn;
        }
        while (nextOff < notes.size() && notes[nextOff].endSample < blockEnd)
        {
            synth.addNoteOff (notes[nextOff].midiNote, notes[nextOff].endSample - offset);
            ++nextOff;
        }

        // The engine renders one block starting at channel[0][0], exactly like a
        // host handing the bus to the plugin.
        float* channels[2] = { left.data() + static_cast<std::size_t> (offset),
                               right.data() + static_cast<std::size_t> (offset) };
        synth.render (channels, 2, n);
    }

    out.m = measureAudio (left, right, setup.sampleRate, notes, false);

    if (setup.keepAudio)
    {
        out.left = std::move (left);
        out.right = std::move (right);
    }

    return out;
}

Rendered renderNote (const PlantSnapshot& plant, int branch, float branchLength,
                     int midiNote, float velocity, double sampleRate, double seconds,
                     bool keepAudio)
{
    RenderSetup setup;
    setup.plant = &plant;
    setup.sampleRate = sampleRate;
    setup.seconds = seconds;
    setup.play = offlinePlayState (branch, branchLength);
    setup.keepAudio = keepAudio;

    // Note on at sample 0, key held for the first 60% of the scene and released
    // on a block boundary, which is the shape every audio gate in this file has
    // always used.
    SceneNote n;
    n.midiNote = midiNote;
    n.velocity = velocity;
    n.startSample = 0;
    n.endSample = static_cast<int> (std::llround (seconds * 0.6 * sampleRate / 128.0) * 128);
    setup.notes.push_back (n);

    return renderScene (setup);
}

//==============================================================================
// WAV
//==============================================================================
namespace
{
void putLE16 (std::vector<uint8_t>& out, uint16_t v)
{
    out.push_back (static_cast<uint8_t> (v & 0xff));
    out.push_back (static_cast<uint8_t> ((v >> 8) & 0xff));
}
void putLE32 (std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back (static_cast<uint8_t> (v & 0xff));
    out.push_back (static_cast<uint8_t> ((v >> 8) & 0xff));
    out.push_back (static_cast<uint8_t> ((v >> 16) & 0xff));
    out.push_back (static_cast<uint8_t> ((v >> 24) & 0xff));
}
uint16_t getLE16 (const uint8_t* p) { return static_cast<uint16_t> (p[0] | (p[1] << 8)); }
uint32_t getLE32 (const uint8_t* p)
{
    return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8)
         | (static_cast<uint32_t> (p[2]) << 16) | (static_cast<uint32_t> (p[3]) << 24);
}
} // namespace

namespace wav
{
std::vector<int16_t> quantise (const std::vector<float>& left, const std::vector<float>& right)
{
    const std::size_t num = std::min (left.size(), right.size());
    std::vector<int16_t> out (num * 2);

    for (std::size_t i = 0; i < num; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            float v = c == 0 ? left[i] : right[i];
            if (! std::isfinite (v))
                v = 0.0f;
            v = std::max (-1.0f, std::min (1.0f, v));
            const float scaled = v * 32767.0f;
            const long q = static_cast<long> (scaled >= 0.0f ? std::floor (scaled + 0.5f)
                                                            : std::ceil (scaled - 0.5f));
            out[i * 2 + static_cast<std::size_t> (c)] = static_cast<int16_t> (q);
        }
    }
    return out;
}

bool write (const std::string& path, int sampleRate,
            const std::vector<int16_t>& interleaved, int channels,
            std::string& error)
{
    if (channels != 1 && channels != 2)
    {
        error = "channel count must be 1 or 2";
        return false;
    }
    if (sampleRate <= 0)
    {
        error = "sample rate must be positive";
        return false;
    }

    const uint32_t dataSize = static_cast<uint32_t> (interleaved.size() * 2);
    const uint16_t blockAlign = static_cast<uint16_t> (channels * 2);

    // WAVE_FORMAT_PCM, canonical 40 byte header. Tests/EngineTests.cpp holds
    // these exact bytes against an expected array; a wrong header is the usual
    // way a green suite turns out to have written silence.
    std::vector<uint8_t> bytes;
    bytes.reserve (dataSize + 44);

    bytes.insert (bytes.end(), { 'R', 'I', 'F', 'F' });
    putLE32 (bytes, 36 + dataSize);
    bytes.insert (bytes.end(), { 'W', 'A', 'V', 'E' });
    bytes.insert (bytes.end(), { 'f', 'm', 't', ' ' });
    putLE32 (bytes, 16);
    putLE16 (bytes, 1);                                   // PCM
    putLE16 (bytes, static_cast<uint16_t> (channels));
    putLE32 (bytes, static_cast<uint32_t> (sampleRate));
    putLE32 (bytes, static_cast<uint32_t> (sampleRate) * blockAlign);
    putLE16 (bytes, blockAlign);
    putLE16 (bytes, 16);
    bytes.insert (bytes.end(), { 'd', 'a', 't', 'a' });
    putLE32 (bytes, dataSize);

    for (const int16_t s : interleaved)
    {
        const uint16_t u = static_cast<uint16_t> (s);
        putLE16 (bytes, u);
    }

    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
    {
        error = "cannot open " + path;
        return false;
    }
    const size_t written = std::fwrite (bytes.data(), 1, bytes.size(), f);
    std::fclose (f);

    if (written != bytes.size())
    {
        error = "short write to " + path;
        return false;
    }
    return true;
}

bool read (const std::string& path, std::vector<int16_t>& interleaved,
           int& channels, int& sampleRate, std::string& error)
{
    interleaved.clear();
    channels = 0;
    sampleRate = 0;

    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
    {
        error = "cannot open " + path;
        return false;
    }
    std::fseek (f, 0, SEEK_END);
    const long size = std::ftell (f);
    std::fseek (f, 0, SEEK_SET);

    std::vector<uint8_t> bytes (static_cast<std::size_t> (size));
    const size_t got = std::fread (bytes.data(), 1, bytes.size(), f);
    std::fclose (f);

    if (got != bytes.size() || bytes.size() < 44)
    {
        error = "file is shorter than a wav header: " + path;
        return false;
    }
    if (std::memcmp (bytes.data(), "RIFF", 4) != 0 || std::memcmp (bytes.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file: " + path;
        return false;
    }

    bool haveFmt = false, haveData = false;
    int bits = 0, fmtChannels = 0, fmtRate = 0;
    std::vector<uint8_t> data;

    std::size_t p = 12;
    while (p + 8 <= bytes.size())
    {
        const uint32_t chunkSize = getLE32 (bytes.data() + p + 4);
        const std::size_t payload = p + 8;
        if (payload + chunkSize > bytes.size())
            break;

        if (std::memcmp (bytes.data() + p, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            if (getLE16 (bytes.data() + payload) != 1)
            {
                error = "not WAVE_FORMAT_PCM: " + path;
                return false;
            }
            fmtChannels = getLE16 (bytes.data() + payload + 2);
            fmtRate = static_cast<int> (getLE32 (bytes.data() + payload + 4));
            bits = getLE16 (bytes.data() + payload + 14);
            haveFmt = true;
        }
        else if (std::memcmp (bytes.data() + p, "data", 4) == 0)
        {
            data.assign (bytes.begin() + static_cast<std::ptrdiff_t> (payload),
                         bytes.begin() + static_cast<std::ptrdiff_t> (payload + chunkSize));
            haveData = true;
        }

        p = payload + chunkSize + (chunkSize & 1);   // chunks are word aligned
    }

    if (! haveFmt || ! haveData)
    {
        error = "no fmt/data chunk: " + path;
        return false;
    }
    if (bits != 16)
    {
        error = "only 16 bit pcm is readable here";
        return false;
    }
    if (fmtChannels != 1 && fmtChannels != 2)
    {
        error = "only 1 or 2 channels are readable here";
        return false;
    }

    interleaved.resize (data.size() / 2);
    for (std::size_t i = 0; i < interleaved.size(); ++i)
        interleaved[i] = static_cast<int16_t> (getLE16 (data.data() + i * 2));

    channels = fmtChannels;
    sampleRate = fmtRate;
    return true;
}

bool writeFloat (const std::string& path, double sampleRate,
                 const std::vector<float>& left, const std::vector<float>& right,
                 std::string& error)
{
    return write (path, static_cast<int> (sampleRate), quantise (left, right), 2, error);
}

bool readFloat (const std::string& path, Data& out, std::string& error)
{
    std::vector<int16_t> pcm;
    if (! read (path, pcm, out.channels, out.sampleRate, error))
        return false;

    const std::size_t frames = pcm.size() / static_cast<std::size_t> (out.channels);
    out.left.resize (frames);
    out.right.resize (frames);

    for (std::size_t i = 0; i < frames; ++i)
    {
        out.left[i] = static_cast<float> (pcm[i * static_cast<std::size_t> (out.channels)]) / 32767.0f;
        out.right[i] = out.channels > 1
            ? static_cast<float> (pcm[i * static_cast<std::size_t> (out.channels) + 1]) / 32767.0f
            : out.left[i];
    }
    return true;
}

std::string bytesHash (const void* data, size_t numBytes)
{
    uint64_t h = 0xCBF29CE484222325ull;
    const uint8_t* p = static_cast<const uint8_t*> (data);
    for (std::size_t i = 0; i < numBytes; ++i)
    {
        h ^= p[i];
        h *= 0x100000001B3ull;
    }

    char hex[17];
    std::snprintf (hex, sizeof (hex), "%016llx", static_cast<unsigned long long> (h));
    return hex;
}

std::string fileHash (const std::string& path)
{
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (f == nullptr)
        return "unreadable";

    uint64_t h = 0xCBF29CE484222325ull;
    std::vector<uint8_t> chunk (65536);
    size_t got;
    while ((got = std::fread (chunk.data(), 1, chunk.size(), f)) > 0)
    {
        for (size_t i = 0; i < got; ++i)
        {
            h ^= chunk[i];
            h *= 0x100000001B3ull;
        }
    }
    std::fclose (f);

    char hex[17];
    std::snprintf (hex, sizeof (hex), "%016llx", static_cast<unsigned long long> (h));
    return hex;
}
} // namespace wav

//==============================================================================
// createTest / performTest
//==============================================================================
namespace
{
std::string sidecarFor (const std::string& wavPath)
{
    return wavPath + ".txt";
}

void renderSpecPlayState (const TestSpec& spec, PlayState& play)
{
    play = offlinePlayState();
    for (int b = 0; b < kNumBranches; ++b)
        play.branchLength[static_cast<std::size_t> (b)] = spec.branchLength;
}
} // namespace

bool renderSpecScene (const TestSpec& spec, Rendered& out, std::string& error)
{
    if (spec.sampleRate < 8000.0 || spec.sampleRate > 384000.0)
    {
        error = "sample rate out of range";
        return false;
    }
    if (spec.seconds <= 0.0)
    {
        error = "time must be positive";
        return false;
    }

    const PlantSnapshot plant = seededPlant (spec.seed);

    RenderSetup setup;
    setup.plant = &plant;
    setup.sampleRate = spec.sampleRate;
    setup.seconds = spec.seconds;
    setup.keepAudio = true;
    renderSpecPlayState (spec, setup.play);

    setup.notes = planScene (hash::combine (spec.seed, hash::fromString ("golden-scene")),
                             spec.sampleRate, spec.seconds, spec.density,
                             static_cast<int> (spec.noteLow), static_cast<int> (spec.noteHigh));

    if (setup.notes.empty())
    {
        error = "the note plan is empty";
        return false;
    }

    out = renderScene (setup);
    return true;
}

GoldenResult createTest (const TestSpec& spec)
{
    GoldenResult r;

    Rendered rendered;
    if (! renderSpecScene (spec, rendered, r.message))
    {
        r.ok = false;
        return r;
    }

    std::string error;
    if (! wav::writeFloat (spec.file, spec.sampleRate, rendered.left, rendered.right, error))
    {
        r.message = error;
        return r;
    }

    // Read back what was just written and measure that, not the buffers in
    // memory: the thing under test here is the file.
    wav::Data file;
    if (! wav::readFloat (spec.file, file, error))
    {
        r.message = "wrote a file we cannot read back: " + error;
        return r;
    }

    r.frames = file.left.size();
    r.hash = wav::fileHash (spec.file);
    r.m = measureAudio (file.left, file.right, static_cast<double> (file.sampleRate), {}, false);

    if (file.sampleRate != static_cast<int> (spec.sampleRate))
    {
        r.message = "the file's header disagrees with the requested sample rate";
        return r;
    }
    if (r.frames != rendered.left.size())
    {
        r.message = "the file does not contain every frame that was rendered";
        return r;
    }
    if (! r.m.finite || r.m.silent)
    {
        r.message = "the file is silent or not finite";
        return r;
    }

    // The hash record (design 13: keep the audio out of git, keep the hashes in
    // a text file). Deterministic, so regenerating a golden can be checked
    // against the committed record without the wav itself.
    std::FILE* f = std::fopen (sidecarFor (spec.file).c_str(), "wb");
    if (f != nullptr)
    {
        std::fprintf (f,
                      "seed %llu time %g sr %g density %g branchLength %.3f frames %zu\n"
                      "fnv1a64 %s\n"
                      "peak %.6f rms %.6f dc %+.6f centroid %.1f flatness %.4f inharmonic %.4f\n",
                      static_cast<unsigned long long> (spec.seed), spec.seconds, spec.sampleRate,
                      spec.density, spec.branchLength, r.frames, r.hash.c_str(),
                      r.m.peak, r.m.rms, r.m.dc, r.m.centroidHz, r.m.flatness, r.m.inharmonicRatio);
        std::fclose (f);
    }

    r.ok = true;
    return r;
}

GoldenResult performTest (const TestSpec& spec)
{
    GoldenResult r;

    std::string error;
    std::vector<int16_t> filePcm;
    int channels = 0, fileRate = 0;
    if (! wav::read (spec.file, filePcm, channels, fileRate, error))
    {
        r.message = error;
        return r;
    }
    if (fileRate != static_cast<int> (spec.sampleRate))
    {
        r.message = "file is " + std::to_string (fileRate) + " Hz, spec asks for "
                  + std::to_string (static_cast<int> (spec.sampleRate));
        return r;
    }
    if (channels != 2)
    {
        r.message = "expected a stereo golden file";
        return r;
    }

    Rendered rendered;
    if (! renderSpecScene (spec, rendered, error))
    {
        r.message = error;
        return r;
    }

    const std::vector<int16_t> fresh = wav::quantise (rendered.left, rendered.right);
    r.frames = filePcm.size() / 2;

    if (fresh.size() != filePcm.size())
    {
        r.message = "frame count differs: rendered " + std::to_string (fresh.size() / 2)
                  + ", file " + std::to_string (filePcm.size() / 2);
        return r;
    }

    // The comparison happens in the PCM16 domain: both sides went through the
    // same quantiser, so "bit exact" (tolerance 0) is achievable and means
    // something. tolerance is in samples, like the reference protocol's 0.001.
    double sumSq = 0.0;
    for (size_t i = 0; i < filePcm.size(); ++i)
    {
        const int d = std::abs (static_cast<int> (fresh[i]) - static_cast<int> (filePcm[i]));
        r.maxDiffLsb = std::max (r.maxDiffLsb, d);
        const double df = static_cast<double> (d) / 32767.0;
        r.maxSampleDiff = std::max (r.maxSampleDiff, df);
        sumSq += df * df;
    }
    r.rmsDiff = std::sqrt (sumSq / static_cast<double> (std::max<size_t> (1, filePcm.size())));
    r.hash = wav::fileHash (spec.file);

    wav::Data file;
    if (wav::readFloat (spec.file, file, error))
        r.m = measureAudio (file.left, file.right, static_cast<double> (fileRate), {}, false);

    r.ok = r.maxSampleDiff <= spec.tolerance && r.m.finite && ! r.m.silent;
    if (! r.ok && r.message.empty())
        r.message = "differs beyond tolerance, or the file is not audible";
    return r;
}

//==============================================================================
// massiveAudioTest
//==============================================================================
SoakResult massiveAudioTest (int rounds, double secondsPerRound, bool verbose)
{
    SoakResult s;
    s.rounds = rounds;

    // "random sr < 100 kHz" (design 13). A list, not a uniform draw, so the
    // awkward rates (22 050, and the non power of two 44 100/88 200) are always
    // hit rather than being rare.
    static const double rates[] = { 22050.0, 32000.0, 44100.0, 48000.0, 64000.0, 88200.0, 96000.0 };

    for (int round = 0; round < rounds; ++round)
    {
        Rng rng (hash::combine (hash::fromString ("massiveAudioTest"),
                                static_cast<uint64_t> (round)));

        const uint64_t seed = rng.nextUint64();
        const double sr = rates[rng.integer (static_cast<int> (sizeof (rates) / sizeof (rates[0])))];
        const double density = static_cast<double> (rng.integer (11));   // 0..10
        const double notesPerSecond = density * density + 1.0;

        MutationParams mp;
        mp.atonality = rng.range (0.0f, 1.0f);

        PlantSnapshot plant = seededPlant (seed, mp);

        RenderSetup setup;
        setup.plant = &plant;
        setup.sampleRate = sr;
        setup.seconds = secondsPerRound;
        setup.play = offlinePlayState();
        for (int b = 0; b < kNumBranches; ++b)
            setup.play.branchLength[static_cast<std::size_t> (b)] = rng.range (0.0f, 1.0f);
        setup.notes = planScene (hash::combine (seed, hash::fromString ("soak-scene")),
                                 sr, secondsPerRound, notesPerSecond);

        const Rendered r = renderScene (setup);
        ++s.unusable;
        bool usable = true;

        if (! r.m.finite)
        {
            ++s.nan;
            usable = false;
        }
        if (r.m.silent)
        {
            ++s.silent;
            usable = false;
        }
        if (std::abs (r.m.dc) > 0.05f)
        {
            ++s.dc;
            usable = false;
            s.worstDc = std::max (s.worstDc, std::abs (r.m.dc));
        }
        if (r.m.peak >= 1.0f)
        {
            ++s.clipped;
            usable = false;
        }
        if (r.m.peak > 0.995f)
        {
            ++s.tooLoud;
            s.worstPeak = std::max (s.worstPeak, r.m.peak);
        }

        if (! usable && verbose)
        {
            std::printf ("       round %d seed %llu sr %.0f density %.1f -> peak %.4f rms %.5f "
                         "dc %+.5f\n",
                         round, static_cast<unsigned long long> (seed), sr, notesPerSecond,
                         r.m.peak, r.m.rms, r.m.dc);
        }
        if (usable)
            --s.unusable;
    }

    return s;
}

} // namespace gmosh
