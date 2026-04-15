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
    float pitchRatio = 2.0f; // default: +1 octave so it's obvious
};

// ============================ Granular pitch backend ============================
// Dual read-head delay/grain pitch shifter.
//
// Notes:
// - This introduces a small algorithmic latency roughly equal to grainLengthSamples.
// - For shimmer-style echo stepping, call OctaveEchoPitchShifter::OnNewEchoBoundary()
//   to change pitch ratio per echo, but this backend itself is continuous and will adapt.
class GranularPitchBackend : public IPitchShifterBackend
{
    struct ReadHead
    {
        float ReadIndex = 0.0f;
    };

public:
    GranularPitchBackend()
    {
    }

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        juce::ignoreUnused(maximumBlockSize);

        sampleRate = newSampleRate;

        const int bufferMilliseconds = 200;
        const int bufferSizeSamples = std::max(1024, static_cast<int>(std::ceil((bufferMilliseconds * sampleRate) / 1000.0)));

        buffer.assign(static_cast<size_t>(bufferSizeSamples), 0.0f);

        SetGrainLengthMilliseconds(30.0f);
        Reset();
    }

    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        writeIndex = 0;
        smoothedPitchRatio = 1.0f;

        // Start the two grains half a grain-length apart in phase so their
        // crossfade windows always sum to unity.
        grainPhaseA = 0.0f;
        grainPhaseB = 0.5f;

        anchorHeadToWrite(headA);
        anchorHeadToWrite(headB);
    }

    float ProcessSample(float inputSample, float pitchRatio) override
    {
        if (buffer.empty())
            return inputSample;

        const float clampedPitchRatio = juce::jlimit(0.25f, 4.0f, pitchRatio);
        smoothPitchRatio(clampedPitchRatio);

        // Write incoming sample
        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        writeIndex = (writeIndex + 1) % static_cast<int>(buffer.size());

        const float phaseIncrement = 1.0f / static_cast<float>(grainLengthSamples);

        // Advance grain A
        const float prevPhaseA = grainPhaseA;
        grainPhaseA += phaseIncrement;

        if (grainPhaseA >= 1.0f)
        {
            grainPhaseA -= 1.0f;
            anchorHeadToWrite(headA);
        }

        // Advance grain B
        const float prevPhaseB = grainPhaseB;
        grainPhaseB += phaseIncrement;

        if (grainPhaseB >= 1.0f)
        {
            grainPhaseB -= 1.0f;
            anchorHeadToWrite(headB);
        }

        // Read from each head
        const float sampleA = readLinear(headA.ReadIndex);
        const float sampleB = readLinear(headB.ReadIndex);

        // Advance read positions by the pitch ratio
        headA.ReadIndex = wrapReadIndex(headA.ReadIndex + smoothedPitchRatio);
        headB.ReadIndex = wrapReadIndex(headB.ReadIndex + smoothedPitchRatio);

        // Hann windows offset by half a period guarantee unity sum:
        // hann(phase) + hann(phase + 0.5) = 1.0
        const float windowA = hannWindow(grainPhaseA);
        const float windowB = hannWindow(grainPhaseB);

        return (sampleA * windowA) + (sampleB * windowB);
    }

    void SetGrainLengthMilliseconds(float newGrainLengthMilliseconds)
    {
        const float clamped = juce::jlimit(5.0f, 80.0f, newGrainLengthMilliseconds);
        grainLengthSamples = std::max(16, static_cast<int>(std::round((clamped * sampleRate) / 1000.0)));
    }

private:
    void smoothPitchRatio(float targetPitchRatio)
    {
        // Much faster coefficient — converges in ~100 samples rather than thousands.
        const float smoothingCoefficient = 0.05f;
        smoothedPitchRatio += smoothingCoefficient * (targetPitchRatio - smoothedPitchRatio);
    }

    void anchorHeadToWrite(ReadHead& readHead)
    {
        // Place the read head a safe distance behind the write head.
        // For pitch ratios up to 4x, the read head advances at most 4x per sample,
        // so over one grain it travels grainLength * 4 samples.
        // We need the head to never catch the write pointer during a grain's lifetime.
        // A lookback of grainLength * (maxRatio + 1) is safe.
        const float lookbackSamples = static_cast<float>(grainLengthSamples) * 5.0f;
        const float writeFloat = static_cast<float>(writeIndex);
        readHead.ReadIndex = wrapReadIndex(writeFloat - lookbackSamples);
    }

    static float hannWindow(float phase01)
    {
        // Hann window: w(t) = 0.5 - 0.5 * cos(2 * pi * t)
        // Key property: hann(t) + hann(t + 0.5) = 1.0 for all t.
        const float twoPi = 2.0f * juce::MathConstants<float>::pi;
        return 0.5f - 0.5f * std::cos(twoPi * phase01);
    }

    float wrapReadIndex(float index) const
    {
        const float size = static_cast<float>(buffer.size());
        float out = index;

        while (out < 0.0f)
            out += size;

        while (out >= size)
            out -= size;

        return out;
    }

    float readLinear(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());
        const float wrapped = wrapReadIndex(readIndexFloat);

        const int index0 = static_cast<int>(std::floor(wrapped));
        const int index1 = (index0 + 1) % size;
        const float fraction = wrapped - static_cast<float>(index0);

        return buffer[static_cast<size_t>(index0)] * (1.0f - fraction)
             + buffer[static_cast<size_t>(index1)] * fraction;
    }

    double sampleRate = 48000.0;

    std::vector<float> buffer;
    int writeIndex = 0;

    int grainLengthSamples = 1440;

    ReadHead headA;
    ReadHead headB;

    float grainPhaseA = 0.0f;
    float grainPhaseB = 0.5f;

    float smoothedPitchRatio = 1.0f;
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
        auto Backend = std::make_unique<GranularPitchBackend>();
        Backend->SetGrainLengthMilliseconds(50.0f);

        //auto ConstantSequence = std::make_unique<ConstantRatioSequence>();
        //ConstantSequence->SetPitchRatio(2.0f); // very obvious

        auto Progressive = std::make_unique<ProgressiveOctaveSequence>();
        Progressive->SetStepOctaves(1);      // +1 octave per echo (use -1 for downward)
        Progressive->SetMaxAbsOctaves(4);    // clamp at ±4 octaves (48 semitones)

        SetSequence(std::move(Progressive));
        SetBackend(std::move(Backend));
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

        if (backend != nullptr)
            backend->Reset();

        //enabled.store(false, std::memory_order_release);
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

    void SetBackend(std::unique_ptr<IPitchShifterBackend>&& newBackend)
    {
        backend = std::move(newBackend);

        if (backend != nullptr)
        {
            backend->Prepare(sampleRate, maximumBlockSizeCached);
            backend->Reset();
        }
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