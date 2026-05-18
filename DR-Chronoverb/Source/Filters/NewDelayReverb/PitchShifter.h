#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <memory>
#include <cmath>
#include <cstdlib>
#include <limits.h>

// ============================ Pitch ratio sequence API ============================

class IPitchSequence
{
public:
    virtual ~IPitchSequence() = default;

    virtual void Reset()
    {
    }

    virtual void AdvanceToNextEcho()
    {
    }

    // Pitch ratio for current echo (1.0 = unison, 2.0 = +1 octave, 0.5 = -1 octave).
    virtual float GetCurrentPitchRatio() const = 0;
};

// Progressive octaves: starts at startOctave, steps by stepOctaves per echo,
// clamped to [lowerBound, upperBound].
class ProgressiveOctaveSequence : public IPitchSequence
{
public:
    ProgressiveOctaveSequence()
    {
    }

    // Sets the inclusive octave range for clamping.
    void SetRange(int newLowerBound, int newUpperBound)
    {
        lowerBound = std::min(newLowerBound, newUpperBound);
        upperBound = std::max(newLowerBound, newUpperBound);
    }

    // Sets the octave value the sequence starts at on Reset().
    void SetStartOctave(int newStartOctave)
    {
        startOctave = newStartOctave;
    }

    // Positive = step up each echo, negative = step down each echo.
    void SetStepOctaves(int newStepOctaves)
    {
        stepOctaves = newStepOctaves;
    }

    void Reset() override
    {
        currentEchoIndex = 0;
        currentOctaves = startOctave;
    }

    void AdvanceToNextEcho() override
    {
        ++currentEchoIndex;
        const int nextOctaves = currentOctaves + stepOctaves;
        currentOctaves = std::max(lowerBound, std::min(upperBound, nextOctaves));
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
    RandomOctaveSequence()
    {
        BuildOctaveList();
    }

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

        for (int octave = lowerBound; octave <= upperBound; ++octave)
            octaves.push_back(octave);
    }

    int PickRandom(int excludeOctave) const
    {
        if (octaves.empty())
            return 0;

        if (static_cast<int>(octaves.size()) == 1)
            return octaves[0];

        std::vector<int> candidates;
        candidates.reserve(octaves.size());

        for (int octave : octaves)
        {
            if (octave != excludeOctave)
                candidates.push_back(octave);
        }

        if (candidates.empty())
            return octaves[0];

        const int selectedIndex = rand() % static_cast<int>(candidates.size());
        return candidates[static_cast<size_t>(selectedIndex)];
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
        (void)newSampleRate;
        (void)maximumBlockSize;
    }

    virtual void Reset()
    {
    }

    virtual float ProcessSample(float inputSample, float pitchRatio) = 0;
};

class ConstantRatioSequence : public IPitchSequence
{
public:
    ConstantRatioSequence()
    {
    }

    void SetPitchRatio(float newPitchRatio)
    {
        pitchRatio = newPitchRatio;
    }

    void Reset() override
    {
    }

    void AdvanceToNextEcho() override
    {
    }

    float GetCurrentPitchRatio() const override
    {
        return pitchRatio;
    }

private:
    float pitchRatio = 1.0f;
};

// ============================ Granular pitch backend (echo-quantized) ============================
// Echo-quantized design:
// - Host calls OnEchoBoundary() exactly at each delay write boundary.
// - Backend latches a pending ratio ONLY at boundary.
// - ProcessSample() uses latched ratio for full echo period (no mid-echo modulation).
// - Optional very short crossfade on boundary to reduce click when ratio jumps.
class GranularPitchBackend : public IPitchShifterBackend
{
    struct ReadHead
    {
        float readIndex = 0.0f;
    };

    struct GrainState
    {
        ReadHead headA;
        ReadHead headB;
        float phaseA = 0.0f;
        float phaseB = 0.5f;
        float ratio = 1.0f;
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
        SetBoundaryCrossfadeMilliseconds(1.0f); // was 4ms

        Reset();
    }

    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;

        pendingRatio = 1.0f;
        hasPendingRatio = false;

        stateA = {};
        stateB = {};

        stateA.phaseA = 0.0f;
        stateA.phaseB = 0.5f;
        stateA.ratio = 1.0f;

        stateB = stateA;

        anchorStateToWrite(stateA);
        anchorStateToWrite(stateB);

        crossfadeRemainingSamples = 0;
        crossfadeTotalSamples = boundaryCrossfadeSamples;
        activeIsA = true;
    }

    // Called ONLY by host at echo boundaries.
    void OnEchoBoundary()
    {
        if (!hasPendingRatio)
            return;

        const float newRatio = pendingRatio;
        hasPendingRatio = false;

        GrainState& active = (activeIsA ? stateA : stateB);
        GrainState& inactive = (activeIsA ? stateB : stateA);

        if (std::abs(newRatio - active.ratio) < 1.0e-6f)
            return;

        // STRICT: clone exact running state, only change ratio
        inactive = active;
        inactive.ratio = newRatio;

        // DO NOT re-anchor here (causes discontinuity/noisy instability)
        // reanchorStateHeadsPreservePhase(inactive);  <-- remove

        crossfadeTotalSamples = boundaryCrossfadeSamples;
        crossfadeRemainingSamples = crossfadeTotalSamples;
        pendingFlipAfterFade = true;
    }

    float ProcessSample(float inputSample, float pitchRatio) override
    {
        if (buffer.empty())
            return inputSample;

        // Stage ratio for NEXT boundary only.
        pendingRatio = juce::jlimit(0.25f, 4.0f, pitchRatio);
        hasPendingRatio = true;

        // Write input
        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());

        GrainState& active = (activeIsA ? stateA : stateB);
        GrainState& inactive = (activeIsA ? stateB : stateA);

        // Always advance active.
        const float outActive = processStateOneSample(active);

        // If crossfading, also advance inactive and blend.
        if (crossfadeRemainingSamples > 0)
        {
            const float outInactive = processStateOneSample(inactive);

            const int done = crossfadeTotalSamples - crossfadeRemainingSamples;
            const float t = static_cast<float>(done)
                          / static_cast<float>(std::max(1, crossfadeTotalSamples));

            const float gOld = std::cos(t * juce::MathConstants<float>::halfPi);
            const float gNew = std::sin(t * juce::MathConstants<float>::halfPi);

            --crossfadeRemainingSamples;

            if (crossfadeRemainingSamples == 0 && pendingFlipAfterFade)
            {
                activeIsA = !activeIsA;
                pendingFlipAfterFade = false;
            }

            // old=currently active, new=inactive candidate
            return outActive * gOld + outInactive * gNew;
        }

        return outActive;
    }

    void SetGrainLengthMilliseconds(float ms)
    {
        const float clamped = juce::jlimit(5.0f, 120.0f, ms);
        grainLengthSamples = std::max(16, static_cast<int>(std::round((clamped * sampleRate) / 1000.0)));
    }

    void SetJitterPercent(float p)
    {
        jitterPercent = juce::jlimit(0.0f, 0.5f, p);
    }

    void SetLookbackMultiplier(float m)
    {
        lookbackMultiplier = juce::jlimit(2.0f, 6.0f, m);
    }

    void SetBoundaryCrossfadeMilliseconds(float ms)
    {
        const float clamped = juce::jlimit(0.0f, 30.0f, ms);
        boundaryCrossfadeSamples = std::max(
            0, static_cast<int>(std::round((clamped * sampleRate) / 1000.0)));
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

    void reanchorStateHeadsPreservePhase(GrainState& s)
    {
        // Preserve phase for smoother envelope continuity, only move read anchors.
        anchorHeadToWrite(s.headA, generateJitterSamples());
        anchorHeadToWrite(s.headB, generateJitterSamples());
    }

    void anchorHeadToWrite(ReadHead& h, float jitterOffsetSamples)
    {
        const float lookback = static_cast<float>(grainLengthSamples) * lookbackMultiplier;
        const float w = static_cast<float>(writeIndex);
        h.readIndex = wrapReadIndex(w - lookback + jitterOffsetSamples);
    }

    float generateJitterSamples() const
    {
        const float u = (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * 2.0f - 1.0f;
        return u * jitterPercent * static_cast<float>(grainLengthSamples);
    }

    static float hannWindow(float phase01)
    {
        const float twoPi = 2.0f * juce::MathConstants<float>::pi;
        return 0.5f - 0.5f * std::cos(twoPi * phase01);
    }

    float wrapReadIndex(float idx) const
    {
        const float size = static_cast<float>(buffer.size());
        float out = idx;
        while (out < 0.0f) out += size;
        while (out >= size) out -= size;
        return out;
    }

    float readCubic(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());
        const float wrapped = wrapReadIndex(readIndexFloat);

        const int i1 = static_cast<int>(std::floor(wrapped));
        const float frac = wrapped - static_cast<float>(i1);

        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;

        const float y0 = buffer[static_cast<size_t>(i0)];
        const float y1 = buffer[static_cast<size_t>(i1)];
        const float y2 = buffer[static_cast<size_t>(i2)];
        const float y3 = buffer[static_cast<size_t>(i3)];

        const float a0 = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
        const float a1 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float a2 = -0.5f * y0 + 0.5f * y2;
        const float a3 = y1;

        return ((a0 * frac + a1) * frac + a2) * frac + a3;
    }

private:
    double sampleRate = 48000.0;
    std::vector<float> buffer;
    int writeIndex = 0;

    int grainLengthSamples = 1680;
    float jitterPercent = 0.12f;
    float lookbackMultiplier = 3.0f;

    GrainState stateA;
    GrainState stateB;
    bool activeIsA = true;

    float pendingRatio = 1.0f;
    bool hasPendingRatio = false;

    int boundaryCrossfadeSamples = 0;
    int crossfadeTotalSamples = 0;
    int crossfadeRemainingSamples = 0;
    bool pendingFlipAfterFade = false;
};

// Passthrough backend for testing/bypassing without touching architecture.
class PassthroughPitchBackend : public IPitchShifterBackend
{
public:
    PassthroughPitchBackend()
    {
    }

    float ProcessSample(float inputSample, float pitchRatio) override
    {
        (void)pitchRatio;
        return inputSample;
    }
};

// ============================ Octave-per-echo pitch extension ============================
// Integrates into NewDelayReverb feedback path.
// Owns a sequence + backend; exposes OnNewEchoBoundary() so the delay can increment pitch.

class OctaveEchoPitchShifter
{
public:
    OctaveEchoPitchShifter()
    {
        auto granularBackend = std::make_unique<GranularPitchBackend>();
        granularBackend->SetGrainLengthMilliseconds(35.0f);
        granularBackend->SetJitterPercent(0.15f);
        granularBackend->SetLookbackMultiplier(3.0f);

        auto progressiveSequence = std::make_unique<ProgressiveOctaveSequence>();
        progressiveSequence->SetRange(-2, 2);     // ±2 octaves default
        progressiveSequence->SetStartOctave(0);   // First echo at unison
        progressiveSequence->SetStepOctaves(1);   // Step up each echo

        SetSequence(std::move(progressiveSequence));
        SetBackend(std::move(granularBackend));
    }

    void Prepare(double newSampleRate, int maximumBlockSize)
    {
        sampleRate = newSampleRate;
        maximumBlockSizeCached = maximumBlockSize;

        if (backend != nullptr)
            backend->Prepare(sampleRate, maximumBlockSize);

        Reset();
    }

    void Reset()
    {
        if (sequence != nullptr)
            sequence->Reset();

        // Discard any pending swap on a full reset
        pendingSequence.reset();
        hasPendingSequence = false;

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

    // Called at each echo boundary (already gated to delay period in ProcessBlock).
    // Commits any staged sequence BEFORE advancing, so the new mode takes effect
    // cleanly at the start of the next echo — never mid-echo.
    void OnNewEchoBoundary()
    {
        if (hasPendingSequence && pendingSequence != nullptr)
        {
            sequence = std::move(pendingSequence);
            hasPendingSequence = false;
            // Reset() was already called in SetSequence when the sequence was staged.
            // Do NOT advance here — let the new sequence play from its initial position.
            return;
        }

        if (sequence != nullptr)
            sequence->AdvanceToNextEcho();

        if (auto* granular = dynamic_cast<GranularPitchBackend*>(backend.get()))
            granular->OnEchoBoundary();
    }

    float ProcessSample(float inputSample)
    {
        if (!GetEnabled())
            return inputSample;

        if (sequence == nullptr || backend == nullptr)
            return inputSample;

        const float pitchRatio = hasForcedPitchRatio
           ? forcedPitchRatio
           : sequence->GetCurrentPitchRatio();

        return backend->ProcessSample(inputSample, pitchRatio);
    }

    // Stages a new sequence for commit at the next echo boundary.
    // Safe to call from the audio thread (which is the only caller after the
    // pitchSequenceRebuildPending flag fix above).
    void SetSequence(std::unique_ptr<IPitchSequence> newSequence)
    {
        pendingSequence = std::move(newSequence);

        if (pendingSequence != nullptr)
            pendingSequence->Reset();

        hasPendingSequence = true;
    }

    // Commits a pending sequence immediately, bypassing the echo boundary gate.
    // Only safe to call when the audio thread is not running (i.e. PrepareToPlay).
    void CommitPendingSequenceNow()
    {
        if (hasPendingSequence && pendingSequence != nullptr)
        {
            sequence = std::move(pendingSequence);
            hasPendingSequence = false;
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

    ProgressiveOctaveSequence* GetProgressiveOctaveSequence()
    {
        return dynamic_cast<ProgressiveOctaveSequence*>(sequence.get());
    }

    GranularPitchBackend* GetGranularBackend()
    {
        return dynamic_cast<GranularPitchBackend*>(backend.get());
    }

    float GetLatencyMilliseconds() const
    {
        auto* granularBackend = dynamic_cast<GranularPitchBackend*>(backend.get());

        if (granularBackend != nullptr && GetEnabled())
            return granularBackend->GetLatencyMilliseconds();

        return 0.0f;
    }

    float GetCurrentPitchRatio() const
    {
        if (sequence == nullptr) return 1.0f;
        return sequence->GetCurrentPitchRatio();
    }

    void SetForcedPitchRatio(float ratio)
    {
        forcedPitchRatio = juce::jlimit(0.25f, 4.0f, ratio);
        hasForcedPitchRatio = true;
    }

    void ClearForcedPitchRatio()
    {
        hasForcedPitchRatio = false;
    }

private:
    double sampleRate = 48000.0;
    int maximumBlockSizeCached = 0;

    bool hasPendingSequence = false;
    bool hasForcedPitchRatio = false;

    float forcedPitchRatio = 1.0f;

    std::unique_ptr<IPitchSequence> sequence;
    std::unique_ptr<IPitchSequence> pendingSequence;

    std::unique_ptr<IPitchShifterBackend> backend;

    std::atomic<bool> enabled { false };
};