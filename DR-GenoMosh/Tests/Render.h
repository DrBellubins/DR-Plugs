#pragma once

#include "Engine/Synth.h"
#include "Genetic/PlantEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gmosh
//==============================================================================
// The design doc section 13 offline protocol (plan milestone M2).
//
//   render a seeded scene offline -> measure it -> write it to WAV -> compare it
//   against a file with a tolerance.
//
// This is the only place audio is *measured* in this project. An assertion about
// a waveform belongs in code, not in an ear, and it belongs here so that the
// same numbers are printed for a unit test, for the 12 x 5 batch and for a
// command line run.
//
// Deliberately dependency free: no JUCE, no FFTW, no standard library
// distributions. The FFT is a plain radix-2 and every random choice comes from
// gmosh::Rng, so a scene and its metrics reproduce bit for bit at a given sample
// rate. Note positions are integers in samples derived from seconds at the
// scene's sample rate, which is what makes "same inputs, identical output"
// mean something.
//
// Two things this file is careful about, because both are how a green test suite
// once turned out to have been measuring silence:
//   * the WAV header is written byte by byte and a test holds it against an
//     expected byte sequence;
//   * every metric is checked against synthetic signals with known answers
//     (Tests/EngineTests.cpp, testMetricsOnKnownSignals) before any metric
//     measured from the engine is believed.
//==============================================================================
{
//==============================================================================
// Scene: a seeded note plan (design 13's "density" = notes per second)
//==============================================================================
struct SceneNote
{
    int midiNote = 60;
    float velocity = 0.9f;
    int startSample = 0;   ///< inclusive
    int endSample = 0;     ///< the key comes up here
};

/// The note generator the golden protocol is built on. Average note spacing is
/// 1/density seconds; holds scale with the spacing so that raising the density
/// raises the note rate rather than the voice count. Deterministic in seedId.
std::vector<SceneNote> planScene (uint64_t seedId, double sampleRate, double seconds,
                                  double notesPerSecond,
                                  int noteLo = 48, int noteHi = 75,
                                  int maxSimultaneous = 6);

//==============================================================================
// Measurement
//==============================================================================
/// One note's worth of measurement, and the thing the articulation matrix of M3
/// reads. Everything is measured on the rendered output, never asked of the
/// engine: the point is to catch the case where the engine and the mapping
/// disagree about what a gene does.
struct NoteMeasurement
{
    bool measured = false;
    int midiNote = 60;
    float f0Hz = 0.0f;             ///< autocorrelation fundamental, 0 = unvoiced
    float centroidHz = 0.0f;
    float flatness = 0.0f;         ///< 1 = noise like, 0 = a single tone
    float inharmonicRatio = 0.0f;  ///< fraction of energy off the f0 harmonic grid
    float peak = 0.0f;
    float attackSeconds = -1.0f;   ///< -1 = never reached the peak level
    bool reachedPeak = true;       ///< false = the envelope never got near its own peak
    float holdLevelDb = 0.0f;      ///< level just before key up, relative to peak
    float releaseSeconds = -1.0f;  ///< -1 = still sounding at the end of the scene
    float endSeconds = 0.0f;       ///< note start -> the sound fell below -40 dB, -1 = never
    float sustainSeconds = 0.0f;   ///< key held down for
    bool selfEnded = false;        ///< fell silent before key up (duration / vol_fade)
};

/// Everything design 13 asks for "per render", aggregated over the notes of the
/// scene. The per note numbers are kept too: a scene average hides the branch
/// that is broken.
struct AudioMetrics
{
    // level / sanity
    bool finite = true;
    float peak = 0.0f, rms = 0.0f, dc = 0.0f;
    bool silent = true;

    // spectral, averaged over measured notes
    float centroidHz = 0.0f, flatness = 0.0f;
    float inharmonicRatio = 0.0f;   ///< the aliasing estimate, see measureNote()
    float loudestNotePeak = 0.0f;

    // articulation
    float attackSeconds = -1.0f;    ///< mean over notes that did reach peak
    float holdLevelDb = 0.0f;
    float releaseSeconds = -1.0f;
    float soundEndSeconds = -1.0f;  ///< mean note start -> sound over, the M3 duration metric
    int selfEndedNotes = 0;

    int notesPlanned = 0, notesMeasured = 0, notesTooShort = 0, notesUnvoiced = 0, notesSilent = 0;

    std::vector<NoteMeasurement> notes;   ///< only for scenes measured per note

    /// "Silence detection" of design 13. Deliberately far above what a real
    /// render of this engine produces (0.07 rms) and far below what a listener
    /// would call a mistake.
    static constexpr float kSilencePeak = 1.0e-4f;

    float dbVs (const AudioMetrics& other) const;
};

/// Measure a rendered scene. Works on engine output and on audio read back from
/// a WAV file, which is how performTest proves that the file it compared also
/// makes sound.
AudioMetrics measureAudio (const std::vector<float>& left, const std::vector<float>& right,
                          double sampleRate, const std::vector<SceneNote>& notes,
                          bool keepPerNote = false);

//==============================================================================
// Render
//==============================================================================
struct RenderSetup
{
    const PlantSnapshot* plant = nullptr;
    double sampleRate = 48000.0;
    double seconds = 2.0;
    int block = 128;
    PlayState play;                      ///< always explicit; see offlinePlayState()
    std::vector<SceneNote> notes;
    bool keepAudio = false;
};

struct Rendered
{
    std::vector<float> left, right;      ///< only when RenderSetup::keepAudio
    AudioMetrics m;

    bool audible () const { return m.finite && ! m.silent; }
};

/// The one render path. Everything audio in the harness goes through here.
Rendered renderScene (const RenderSetup& setup);

/// Design 13's batch mode input: "seed id or genome, branch index, length,
/// note, velocity". The wheel is wide open with wheelScale 1, so branchLength
/// is the literal section 5.2 distance.
Rendered renderNote (const PlantSnapshot& plant, int branch, float branchLength,
                     int midiNote, float velocity, double sampleRate, double seconds,
                     bool keepAudio = false);

/// A PlayState for offline measurement. The plugin's own wheel maths is not part
/// of a growth measurement, so the wheel is pinned fully open and wheelScale to
/// 1: what is in branchLength[] is what the voice sees.
PlayState offlinePlayState();
PlayState offlinePlayState (int branch, float branchLength);

/// A plant from a seed id, the same way the plugin builds one (design 11).
PlantSnapshot seededPlant (uint64_t seedId, const MutationParams& mp = {});

//==============================================================================
// WAV: 16 bit WAVE_FORMAT_PCM, and a reader, because performTest has to read
// back what createTest wrote.
//==============================================================================
namespace wav
{
    /// PCM16 in / float out. Hard clipped at +-1 (the engine's own soft clipper
    /// keeps it inside that already) and rounded away from zero, with no dither:
    /// a dither here would make the file disagree with the render it claims to
    /// be a golden copy of.
    std::vector<int16_t> quantise (const std::vector<float>& left,
                                   const std::vector<float>& right);

    bool write (const std::string& path, int sampleRate,
                const std::vector<int16_t>& interleaved, int channels,
                std::string& error);
    bool read (const std::string& path, std::vector<int16_t>& interleaved,
               int& channels, int& sampleRate, std::string& error);

    /// Convenience: float buffers in, file on disk out.
    bool writeFloat (const std::string& path, double sampleRate,
                     const std::vector<float>& left, const std::vector<float>& right,
                     std::string& error);

    /// Float samples out of a file. Non integer sample rates are reported as an
    /// error rather than rounded: the golden protocol is per sample rate.
    struct Data
    {
        int sampleRate = 0;
        int channels = 0;
        std::vector<float> left, right;
    };
    bool readFloat (const std::string& path, Data& out, std::string& error);

    /// Stable 64 bit content hash (FNV-1a) of the file, printed as hex. Not a
    /// cryptographic digest; it only has to change when the bytes change.
    std::string fileHash (const std::string& path);
    std::string bytesHash (const void* data, size_t numBytes);
}

//==============================================================================
// The golden protocol of design 13:
//   perform('createTest',  { seed, time, sr, density, file })
//   perform('performTest', { ..., tolerance })
//==============================================================================
struct TestSpec
{
    uint64_t seed = 1;
    double seconds = 300.0;         ///< "time"
    double sampleRate = 44100.0;    ///< "sr"
    double density = 10.0;          ///< notes per second
    std::string file;               ///< wav to write / read
    double tolerance = 0.001;       ///< in samples, 0 = bit exact in the pcm domain
    float branchLength = 0.6f;      ///< every branch of the plant is grown this far
    float noteLow = 48, noteHigh = 75;
};

struct GoldenResult
{
    bool ok = false;
    std::string message;

    size_t frames = 0;
    double maxSampleDiff = 0.0, rmsDiff = 0.0;
    int maxDiffLsb = 0;
    std::string hash;
    AudioMetrics m;                 ///< measured on the *file* for performTest
};

/// Render the scene the spec describes. Shared by createTest and performTest, so
/// the two cannot drift apart.
bool renderSpecScene (const TestSpec& spec, Rendered& out, std::string& error);

/// Render, quantise, write the wav, and record a hash + metrics sidecar next to
/// it (design 13: keep the audio out of git, keep the hashes in a text file).
GoldenResult createTest (const TestSpec& spec);

/// Re render and compare against the file, in the PCM16 domain, with the spec's
/// tolerance. Also measures the file itself, so a file that is silent, or a
/// header that lies about the frame count, fails here instead of passing.
GoldenResult performTest (const TestSpec& spec);

/// The design 13 soak: random seed, random sample rate under 100 kHz, density
/// sqr(random(10)) + 1. Returns the number of rounds that produced an unusable
/// render and prints a one line summary per failure class.
struct SoakResult
{
    int rounds = 0, unusable = 0, nan = 0, silent = 0, dc = 0, clipped = 0, tooLoud = 0;
    float worstDbVsSeed = 0.0f, worstDc = 0.0f, worstPeak = 0.0f;
};
SoakResult massiveAudioTest (int rounds, double secondsPerRound = 1.5, bool verbose = false);

} // namespace gmosh
