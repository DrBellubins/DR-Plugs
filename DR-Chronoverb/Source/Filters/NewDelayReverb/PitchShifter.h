#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <climits>

// ============================ Pitch ratio sequence API ============================

class IPitchSequence
{
public:
    virtual ~IPitchSequence() = default;

    virtual void Reset() {}
    virtual void AdvanceToNextEcho() {}

    // Pitch ratio for current echo (1.0 = unison, 2.0 = +1 octave, 0.5 = -1 octave).
    virtual float GetCurrentPitchRatio() const = 0;
};

// Progressive octaves: starts at startOctave, steps by stepOctaves per echo,
// clamped to [lowerBound, upperBound].
class ProgressiveOctaveSequence : public IPitchSequence
{
public:
    ProgressiveOctaveSequence() {}

    void SetRange(int newLowerBound, int newUpperBound)
    {
        lowerBound = std::min(newLowerBound, newUpperBound);
        upperBound = std::max(newLowerBound, newUpperBound);
    }

    void SetStartOctave(int newStartOctave) { startOctave = newStartOctave; }
    void SetStepOctaves(int newStepOctaves) { stepOctaves = newStepOctaves; }

    void Reset() override
    {
        currentEchoIndex = 0;
        currentOctaves = startOctave;
    }

    void AdvanceToNextEcho() override
    {
        ++currentEchoIndex;
        const int next = currentOctaves + stepOctaves;
        currentOctaves = std::max(lowerBound, std::min(upperBound, next));
    }

    float GetCurrentPitchRatio() const override
    {
        const int clamped = juce::jlimit(-4, 4, currentOctaves);
        return std::pow(2.0f, static_cast<float>(clamped));
    }

private:
    int stepOctaves    = 1;
    int startOctave    = 0;
    int lowerBound     = -2;
    int upperBound     =  2;
    int currentEchoIndex = 0;
    int currentOctaves = 0;
};

// Random octave sequence: picks a random octave within [lowerBound, upperBound]
// each echo, avoiding an immediate back-to-back repeat when more than one choice exists.
class RandomOctaveSequence : public IPitchSequence
{
public:
    RandomOctaveSequence() { BuildOctaveList(); }

    void SetRange(int newLowerBound, int newUpperBound)
    {
        lowerBound = std::min(newLowerBound, newUpperBound);
        upperBound = std::max(newLowerBound, newUpperBound);
        BuildOctaveList();
    }

    void Reset() override
    {
        lastOctave = INT_MIN;
        currentOctave = PickRandom(lastOctave);
    }

    void AdvanceToNextEcho() override
    {
        lastOctave = currentOctave;
        currentOctave = PickRandom(lastOctave);
    }

    float GetCurrentPitchRatio() const override
    {
        const int clamped = juce::jlimit(-4, 4, currentOctave);
        return std::pow(2.0f, static_cast<float>(clamped));
    }

private:
    void BuildOctaveList()
    {
        octaves.clear();
        for (int o = lowerBound; o <= upperBound; ++o)
            octaves.push_back(o);
    }

    int PickRandom(int excludeOctave) const
    {
        if (octaves.empty()) return 0;
        if (static_cast<int>(octaves.size()) == 1) return octaves[0];

        std::vector<int> candidates;
        candidates.reserve(octaves.size());
        for (int o : octaves)
            if (o != excludeOctave)
                candidates.push_back(o);

        if (candidates.empty()) return octaves[0];
        return candidates[static_cast<size_t>(rand()) % candidates.size()];
    }

    int lowerBound    = -2;
    int upperBound    =  2;
    int currentOctave =  0;
    int lastOctave    = INT_MIN;
    std::vector<int> octaves;
};

// ============================ Pitch shifter backend API ============================

class IPitchShifterBackend
{
public:
    virtual ~IPitchShifterBackend() = default;

    virtual void Prepare(double newSampleRate, int maximumBlockSize)
    {
        juce::ignoreUnused(newSampleRate, maximumBlockSize);
    }

    virtual void Reset() {}

    // pitchRatio is provided for non-granular backends; granular manages
    // its own ratio via OnEchoBoundary / SetInitialRatio.
    virtual float ProcessSample(float inputSample, float pitchRatio) = 0;
};

class ConstantRatioSequence : public IPitchSequence
{
public:
    void SetPitchRatio(float r) { pitchRatio = r; }
    float GetCurrentPitchRatio() const override { return pitchRatio; }

private:
    float pitchRatio = 1.0f;
};

// ============================ Granular pitch backend (echo-quantized) ============================
// Ratio changes are driven externally by OnEchoBoundary(newRatio).
// ProcessSample's pitchRatio parameter is ignored — the granular backend uses
// only the ratio set at the last boundary call.
class GranularPitchBackend : public IPitchShifterBackend
{
    struct ReadHead { float readIndex = 0.0f; };

    struct GrainState
    {
        ReadHead headA;
        ReadHead headB;
        float phaseA = 0.0f;
        float phaseB = 0.5f;
        float ratio  = 1.0f;
    };

public:
    GranularPitchBackend() = default;

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        juce::ignoreUnused(maximumBlockSize);
        sampleRate = newSampleRate;

        const int bufferMs = 300;
        const int bufferSize = std::max(
            2048,
            static_cast<int>(std::ceil((bufferMs * sampleRate) / 1000.0)));

        buffer.assign(static_cast<size_t>(bufferSize), 0.0f);

        SetGrainLengthMilliseconds(35.0f);
        SetJitterPercent(0.12f);
        SetLookbackMultiplier(3.0f);
        SetBoundaryCrossfadeMilliseconds(1.0f);

        Reset();
    }

    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;

        stateA = {};
        stateB = {};
        stateA.phaseA = 0.0f;
        stateA.phaseB = 0.5f;
        stateA.ratio  = 1.0f;
        stateB = stateA;

        anchorStateToWrite(stateA);
        anchorStateToWrite(stateB);

        crossfadeRemainingSamples = 0;
        crossfadeTotalSamples     = boundaryCrossfadeSamples;
        activeIsA                 = true;
        pendingFlipAfterFade      = false;
    }

    // ------------------------------------------------------------------
    // Called by OctaveEchoPitchShifter AFTER advancing the sequence.
    // newRatio is the ratio the next echo should play at.
    // ------------------------------------------------------------------
    void OnEchoBoundary(float newRatio)
    {
        const float r = juce::jlimit(0.25f, 4.0f, newRatio);

        GrainState& active   = (activeIsA ? stateA : stateB);
        GrainState& inactive = (activeIsA ? stateB : stateA);

        if (std::abs(r - active.ratio) < 1.0e-6f)
            return; // No change needed

        // Clone running grain state, change only the ratio.
        // DO NOT re-anchor read heads — that causes discontinuity.
        inactive       = active;
        inactive.ratio = r;

        crossfadeTotalSamples     = boundaryCrossfadeSamples;
        crossfadeRemainingSamples = crossfadeTotalSamples;
        pendingFlipAfterFade      = true;
    }

    // ------------------------------------------------------------------
    // Sets the initial ratio with no crossfade. Call once at PrepareToPlay
    // after CommitPendingSequenceNow to avoid a silent "wrong-pitch" first echo.
    // ------------------------------------------------------------------
    void SetInitialRatio(float ratio)
    {
        const float r  = juce::jlimit(0.25f, 4.0f, ratio);
        stateA.ratio   = r;
        stateB.ratio   = r;
        stateA.phaseA  = 0.0f;
        stateA.phaseB  = 0.5f;
        stateB.phaseA  = 0.0f;
        stateB.phaseB  = 0.5f;

        anchorStateToWrite(stateA);
        anchorStateToWrite(stateB);

        crossfadeRemainingSamples = 0;
        pendingFlipAfterFade      = false;
        activeIsA                 = true;
    }

    // pitchRatio is intentionally unused — ratio is now managed via OnEchoBoundary.
    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        if (buffer.empty())
            return inputSample;

        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());

        GrainState& active   = (activeIsA ? stateA : stateB);
        GrainState& inactive = (activeIsA ? stateB : stateA);

        const float outActive = processStateOneSample(active);

        if (crossfadeRemainingSamples > 0)
        {
            const float outInactive = processStateOneSample(inactive);
            const int done = crossfadeTotalSamples - crossfadeRemainingSamples;
            const float t  = static_cast<float>(done)
                           / static_cast<float>(std::max(1, crossfadeTotalSamples));

            const float gOld = std::cos(t * juce::MathConstants<float>::halfPi);
            const float gNew = std::sin(t * juce::MathConstants<float>::halfPi);

            --crossfadeRemainingSamples;

            if (crossfadeRemainingSamples == 0 && pendingFlipAfterFade)
            {
                activeIsA            = !activeIsA;
                pendingFlipAfterFade = false;
            }

            return outActive * gOld + outInactive * gNew;
        }

        return outActive;
    }

    void SetGrainLengthMilliseconds(float ms)
    {
        const float clamped = juce::jlimit(5.0f, 120.0f, ms);
        grainLengthSamples  = std::max(16, static_cast<int>(
            std::round((clamped * sampleRate) / 1000.0)));
    }

    void SetJitterPercent(float p)     { jitterPercent      = juce::jlimit(0.0f, 0.5f, p); }
    void SetLookbackMultiplier(float m){ lookbackMultiplier = juce::jlimit(2.0f, 6.0f, m); }

    void SetBoundaryCrossfadeMilliseconds(float ms)
    {
        const float clamped      = juce::jlimit(0.0f, 30.0f, ms);
        boundaryCrossfadeSamples = std::max(0, static_cast<int>(
            std::round((clamped * sampleRate) / 1000.0)));
    }

    float GetLatencyMilliseconds() const
    {
        const float lookbackSamples = static_cast<float>(grainLengthSamples) * lookbackMultiplier;
        return (lookbackSamples * 1000.0f) / static_cast<float>(sampleRate);
    }

private:
    float processStateOneSample(GrainState& s)
    {
        const float sampleA = readCubic(s.headA.readIndex);
        const float sampleB = readCubic(s.headB.readIndex);

        s.headA.readIndex = wrapReadIndex(s.headA.readIndex + s.ratio);
        s.headB.readIndex = wrapReadIndex(s.headB.readIndex + s.ratio);

        const float phaseInc = 1.0f / static_cast<float>(grainLengthSamples);

        s.phaseA += phaseInc;
        if (s.phaseA >= 1.0f)
        {
            s.phaseA -= 1.0f;
            anchorHeadToWrite(s.headA, generateJitterSamples());
        }

        s.phaseB += phaseInc;
        if (s.phaseB >= 1.0f)
        {
            s.phaseB -= 1.0f;
            anchorHeadToWrite(s.headB, generateJitterSamples());
        }

        const float wA = hannWindow(s.phaseA);
        const float wB = hannWindow(s.phaseB);
        return sampleA * wA + sampleB * wB;
    }

    void anchorStateToWrite(GrainState& s)
    {
        anchorHeadToWrite(s.headA, 0.0f);
        anchorHeadToWrite(s.headB, 0.0f);
    }

    void anchorHeadToWrite(ReadHead& h, float jitterOffsetSamples)
    {
        const float lookback = static_cast<float>(grainLengthSamples) * lookbackMultiplier;
        const float w        = static_cast<float>(writeIndex);
        h.readIndex          = wrapReadIndex(w - lookback + jitterOffsetSamples);
    }

    float generateJitterSamples() const
    {
        const float u = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
        return u * jitterPercent * static_cast<float>(grainLengthSamples);
    }

    static float hannWindow(float phase01)
    {
        return 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * phase01);
    }

    float wrapReadIndex(float idx) const
    {
        const float size = static_cast<float>(buffer.size());
        float out = idx;
        while (out < 0.0f)    out += size;
        while (out >= size)   out -= size;
        return out;
    }

    float readCubic(float readIndexFloat) const
    {
        const int   size    = static_cast<int>(buffer.size());
        const float wrapped = wrapReadIndex(readIndexFloat);
        const int   i1      = static_cast<int>(std::floor(wrapped));
        const float frac    = wrapped - static_cast<float>(i1);

        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;

        const float y0 = buffer[static_cast<size_t>(i0)];
        const float y1 = buffer[static_cast<size_t>(i1)];
        const float y2 = buffer[static_cast<size_t>(i2)];
        const float y3 = buffer[static_cast<size_t>(i3)];

        const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float a1 =  y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a2 = -0.5f * y0 + 0.5f * y2;
        const float a3 =  y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

    double sampleRate = 48000.0;
    std::vector<float> buffer;
    int writeIndex = 0;

    int   grainLengthSamples    = 1680;
    float jitterPercent         = 0.12f;
    float lookbackMultiplier    = 3.0f;

    GrainState stateA;
    GrainState stateB;
    bool activeIsA = true;

    int  boundaryCrossfadeSamples  = 0;
    int  crossfadeTotalSamples     = 0;
    int  crossfadeRemainingSamples = 0;
    bool pendingFlipAfterFade      = false;
};

// Passthrough backend (testing / bypass).
class PassthroughPitchBackend : public IPitchShifterBackend
{
public:
    float ProcessSample(float inputSample, float /*pitchRatio*/) override
    {
        return inputSample;
    }
};

// ============================ Octave-per-echo pitch extension ============================

class OctaveEchoPitchShifter
{
public:
    OctaveEchoPitchShifter()
    {
        auto granular = std::make_unique<GranularPitchBackend>();
        granular->SetGrainLengthMilliseconds(35.0f);
        granular->SetJitterPercent(0.15f);
        granular->SetLookbackMultiplier(3.0f);

        auto seq = std::make_unique<ProgressiveOctaveSequence>();
        seq->SetRange(-2, 2);
        seq->SetStartOctave(0);
        seq->SetStepOctaves(1);

        SetSequence(std::move(seq));
        SetBackend(std::move(granular));
    }

    void Prepare(double newSampleRate, int maximumBlockSize)
    {
        sampleRate             = newSampleRate;
        maximumBlockSizeCached = maximumBlockSize;

        if (backend != nullptr)
            backend->Prepare(sampleRate, maximumBlockSize);

        Reset();
    }

    void Reset()
    {
        pendingSequence.reset();
        hasPendingSequence = false;

        if (sequence != nullptr)
            sequence->Reset();

        if (backend != nullptr)
            backend->Reset();
    }

    void SetEnabled(bool shouldBeEnabled)
    {
        enabled.store(shouldBeEnabled, std::memory_order_release);
    }

    bool GetEnabled() const
    {
        return enabled.load(std::memory_order_acquire);
    }

    // -----------------------------------------------------------------------
    // Called at each echo boundary (left channel, or right when stereo is on).
    //
    // Order of operations:
    //   1. If a new sequence is pending, commit it and immediately tell the
    //      granular to cross-fade to the new sequence's starting ratio. Return.
    //   2. Otherwise advance the current sequence, then tell the granular to
    //      cross-fade to the NOW-CURRENT ratio.
    //
    // This fixes the one-echo delay that existed when pendingRatio was used.
    // -----------------------------------------------------------------------
    void OnNewEchoBoundary()
    {
        // Commit a staged sequence change cleanly at the echo boundary.
        if (hasPendingSequence && pendingSequence != nullptr)
        {
            sequence           = std::move(pendingSequence);
            hasPendingSequence = false;
            // Reset() was already called on it inside SetSequence.

            // Tell the granular to cross-fade to the new sequence's start ratio.
            if (auto* g = dynamic_cast<GranularPitchBackend*>(backend.get()))
                g->OnEchoBoundary(sequence->GetCurrentPitchRatio());

            return; // Don't advance yet — let the new sequence start from step 0.
        }

        // Normal boundary: advance the sequence first, then inform the granular
        // of the ratio it should play for the NEXT echo.
        if (sequence != nullptr)
            sequence->AdvanceToNextEcho();

        if (auto* g = dynamic_cast<GranularPitchBackend*>(backend.get()))
            g->OnEchoBoundary(sequence != nullptr ? sequence->GetCurrentPitchRatio() : 1.0f);
    }

    // -----------------------------------------------------------------------
    // Mono-linked mode: right channel mirrors the left channel's new ratio
    // without advancing its own independent sequence.
    // Called from ProcessBlock instead of OnNewEchoBoundary when stereo is off.
    // -----------------------------------------------------------------------
    void OnNewEchoBoundaryMirrored(float mirroredRatio)
    {
        if (auto* g = dynamic_cast<GranularPitchBackend*>(backend.get()))
            g->OnEchoBoundary(mirroredRatio);
    }

    float ProcessSample(float inputSample)
    {
        if (!GetEnabled())
            return inputSample;

        if (sequence == nullptr || backend == nullptr)
            return inputSample;

        return backend->ProcessSample(inputSample, sequence->GetCurrentPitchRatio());
    }

    // Stages a new sequence; it will be committed at the next echo boundary.
    void SetSequence(std::unique_ptr<IPitchSequence> newSequence)
    {
        pendingSequence = std::move(newSequence);
        if (pendingSequence != nullptr)
            pendingSequence->Reset();
        hasPendingSequence = true;
    }

    // Commits a pending sequence immediately — only safe outside the audio thread
    // (i.e. in PrepareToPlay). Also syncs the granular to the sequence's starting
    // ratio with no crossfade so the first echo plays at the correct pitch.
    void CommitPendingSequenceNow()
    {
        if (hasPendingSequence && pendingSequence != nullptr)
        {
            sequence           = std::move(pendingSequence);
            hasPendingSequence = false;

            // Directly initialise the granular to the correct starting ratio.
            if (auto* g = dynamic_cast<GranularPitchBackend*>(backend.get()))
                g->SetInitialRatio(sequence->GetCurrentPitchRatio());
        }
    }

    void SetBackend(std::unique_ptr<IPitchShifterBackend>&& newBackend)
    {
        backend = std::move(newBackend);
        if (backend != nullptr)
        {
            backend->Prepare(sampleRate, maximumBlockSizeCached);
            backend->Reset();
        }
    }

    GranularPitchBackend* GetGranularBackend()
    {
        return dynamic_cast<GranularPitchBackend*>(backend.get());
    }

    float GetLatencyMilliseconds() const
    {
        auto* g = dynamic_cast<GranularPitchBackend*>(backend.get());
        if (g != nullptr && GetEnabled())
            return g->GetLatencyMilliseconds();
        return 0.0f;
    }

    float GetCurrentPitchRatio() const
    {
        if (sequence == nullptr) return 1.0f;
        return sequence->GetCurrentPitchRatio();
    }

private:
    double sampleRate             = 48000.0;
    int    maximumBlockSizeCached = 0;
    bool   hasPendingSequence     = false;

    std::unique_ptr<IPitchSequence>      sequence;
    std::unique_ptr<IPitchSequence>      pendingSequence;
    std::unique_ptr<IPitchShifterBackend> backend;

    std::atomic<bool> enabled { false };
};