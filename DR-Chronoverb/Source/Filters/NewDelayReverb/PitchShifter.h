#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <atomic>

// ============================ Pitch ratio sequence API ============================
// The sequence decides which pitch ratio applies to the current echo iteration.
// Later you can add Random, PingPong, CustomPattern, MIDI-scale quantized, etc.

class IPitchSequence
{
public:
    virtual ~IPitchSequence() = default;

    virtual void Reset()
    {
    }

    // Called when a new echo iteration begins (typically at the delay wrap / tap event).
    virtual void AdvanceToNextEcho()
    {
    }

    // Pitch ratio for current echo (1.0 = unison, 2.0 = +1 octave, 0.5 = -1 octave).
    virtual float GetCurrentPitchRatio() const = 0;
};

// Progressive octaves: 0, +1, +2, +3... (or negative if stepOctaves is negative).
class ProgressiveOctaveSequence : public IPitchSequence
{
public:
    ProgressiveOctaveSequence()
    {
    }

    void SetStepOctaves(int stepOctavesIn)
    {
        stepOctaves = stepOctavesIn;
    }

    void SetMaxAbsOctaves(int maxAbsOctavesIn)
    {
        maxAbsOctaves = std::max(0, maxAbsOctavesIn);
    }

    void Reset() override
    {
        currentEchoIndex = 0;
        currentOctaves = 0;
    }

    void AdvanceToNextEcho() override
    {
        ++currentEchoIndex;

        const int nextOctaves = currentOctaves + stepOctaves;

        if (maxAbsOctaves > 0)
        {
            currentOctaves = std::max(-maxAbsOctaves, std::min(maxAbsOctaves, nextOctaves));
        }
        else
        {
            currentOctaves = nextOctaves;
        }
    }

    float GetCurrentPitchRatio() const override
    {
        // ratio = 2^(octaves)
        return std::pow(2.0f, static_cast<float>(currentOctaves));
    }

private:
    int stepOctaves = 1;
    int maxAbsOctaves = 0; // 0 = unlimited

    int currentEchoIndex = 0;
    int currentOctaves = 0;
};

// ============================ Pitch shifter backend API ============================
// Backend is responsible for actually shifting pitch by ratio.
// Keep it interface-based so you can replace implementation without touching the sequencing.

class IPitchShifterBackend
{
public:
    virtual ~IPitchShifterBackend() = default;

    virtual void Prepare(double sampleRate, int maximumBlockSize)
    {
        (void)sampleRate;
        (void)maximumBlockSize;
    }

    virtual void Reset()
    {
    }

    // Process a single sample for now. If you later want a better algorithm, switch to block processing.
    virtual float ProcessSample(float inputSample, float pitchRatio) = 0;
};

// Placeholder backend: currently does NO pitch shifting (passes through).
// This lets you wire the architecture now and drop in a real algorithm later.
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
// This is the class you integrate into NewDelayReverb feedback path.
// It owns a sequence + backend and exposes "OnNewEchoBoundary" so your delay can increment.

class OctaveEchoPitchShifter
{
public:
    OctaveEchoPitchShifter()
    {
        SetSequence(std::make_unique<ProgressiveOctaveSequence>());
        SetBackend(std::make_unique<PassthroughPitchBackend>());
    }

    void Prepare(double newSampleRate, int maximumBlockSize)
    {
        sampleRate = newSampleRate;

        if (backend != nullptr)
            backend->Prepare(sampleRate, maximumBlockSize);

        Reset();
    }

    void Reset()
    {
        if (sequence != nullptr)
            sequence->Reset();

        if (backend != nullptr)
            backend->Reset();

        enabled.store(false, std::memory_order_release);
    }

    void SetEnabled(bool shouldBeEnabled)
    {
        enabled.store(shouldBeEnabled, std::memory_order_release);
    }

    bool GetEnabled() const
    {
        return enabled.load(std::memory_order_acquire);
    }

    // Call this at the moment your delay produces a "new echo generation".
    // Practical trigger options:
    // - When your delay write index wraps (fixed delay = easier)
    // - When sampleCounter reaches delaySamples (if you track it)
    // - When you detect the tap boundary you consider "next echo"
    void OnNewEchoBoundary()
    {
        if (sequence != nullptr)
            sequence->AdvanceToNextEcho();
    }

    float ProcessSample(float inputSample)
    {
        if (!GetEnabled())
            return inputSample;

        if (sequence == nullptr || backend == nullptr)
            return inputSample;

        const float pitchRatio = sequence->GetCurrentPitchRatio();
        return backend->ProcessSample(inputSample, pitchRatio);
    }

    // ---- Extension points ----
    void SetSequence(std::unique_ptr<IPitchSequence> newSequence)
    {
        sequence = std::move(newSequence);

        if (sequence != nullptr)
            sequence->Reset();
    }

    void SetBackend(std::unique_ptr<IPitchShifterBackend> newBackend)
    {
        backend = std::move(newBackend);

        if (backend != nullptr)
            backend->Prepare(sampleRate, maximumBlockSizeCached);

        if (backend != nullptr)
            backend->Reset();
    }

    // Convenience access for configuring the default progressive sequence without downcasting elsewhere.
    ProgressiveOctaveSequence* GetProgressiveOctaveSequence()
    {
        return dynamic_cast<ProgressiveOctaveSequence*>(sequence.get());
    }

private:
    double sampleRate = 48000.0;
    int maximumBlockSizeCached = 0;

    std::unique_ptr<IPitchSequence> sequence;
    std::unique_ptr<IPitchShifterBackend> backend;

    std::atomic<bool> enabled { false };
};