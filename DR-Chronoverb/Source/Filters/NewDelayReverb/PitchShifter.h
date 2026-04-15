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
        float Phase01 = 0.0f;
    };

public:
    GranularPitchBackend()
    {
    }

    void Prepare(double newSampleRate, int maximumBlockSize) override
    {
        juce::ignoreUnused(maximumBlockSize);

        sampleRate = newSampleRate;

        // Buffer holds enough for grains + some safety.
        // This is not "the delay time"; it is just the pitch shifter work buffer.
        const int bufferMilliseconds = 200;
        const int bufferSizeSamples = std::max(1024, static_cast<int>(std::ceil((bufferMilliseconds * sampleRate) / 1000.0)));

        buffer.assign(static_cast<size_t>(bufferSizeSamples), 0.0f);

        // Default grain length ~ 30 ms (common starting point).
        // You can make this parameterized later if desired.
        SetGrainLengthMilliseconds(30.0f);

        Reset();
    }

    void Reset() override
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);

        writeIndex = 0;

        lastPitchRatio = 1.0f;
        smoothedPitchRatio = 1.0f;

        headA.Phase01 = 0.0f;
        headB.Phase01 = 0.5f;

        reseedHead(headA, 0.0f);
        reseedHead(headB, static_cast<float>(grainLengthSamples));
    }

    float ProcessSample(float inputSample, float pitchRatio) override
    {
        if (buffer.empty())
        {
            return inputSample;
        }

        const float clampedPitchRatio = juce::jlimit(0.25f, 4.0f, pitchRatio);
        smoothPitchRatio(clampedPitchRatio);

        buffer[static_cast<size_t>(writeIndex)] = inputSample;
        incrementWrite();

        // Advance both head phases; wrap + reseed when a head finishes a grain.
        advanceHeadPhaseAndReseedIfNeeded(headA);
        advanceHeadPhaseAndReseedIfNeeded(headB);

        // Complementary windows: use sin^2/cos^2 (equal-power crossfade)
        const float windowA = windowFadeInEqualPower(headA.Phase01);
        const float windowB = windowFadeOutEqualPower(headA.Phase01);

        const float sampleA = readLinear(headA.ReadIndex);
        const float sampleB = readLinear(headB.ReadIndex);

        // Advance both heads by pitch ratio (fractional)
        headA.ReadIndex = wrapReadIndex(headA.ReadIndex + smoothedPitchRatio);
        headB.ReadIndex = wrapReadIndex(headB.ReadIndex + smoothedPitchRatio);

        // Normalized blend (windows are roughly complementary if phases are offset)
        const float outputSample = (sampleA * windowA) + (sampleB * windowB);
        return outputSample;
    }

    void SetGrainLengthMilliseconds(float newGrainLengthMilliseconds)
    {
        const float clamped = std::max(5.0f, std::min(80.0f, newGrainLengthMilliseconds));
        const int newSamples = std::max(16, static_cast<int>(std::round((clamped * sampleRate) / 1000.0)));

        grainLengthSamples = newSamples;

        // Keep read heads consistent with the new grain.
        //reanchorReadHeads();
    }

private:
    void smoothPitchRatio(float targetPitchRatio)
    {
        // One-pole smoothing. Adjust coefficient to taste.
        // Smaller coefficient => smoother but slower response.
        const float smoothing = 0.0025f;

        lastPitchRatio = targetPitchRatio;
        smoothedPitchRatio = smoothedPitchRatio + smoothing * (targetPitchRatio - smoothedPitchRatio);
    }

    static float fract01(float value)
    {
        float out = value - std::floor(value);
        if (out < 0.0f)
        {
            out += 1.0f;
        }
        return out;
    }

    static float hann01(float phase01In)
    {
        // Standard Hann window in [0..1]:
        // w = 0.5 - 0.5*cos(2*pi*phase)
        const float twoPi = 2.0f * juce::MathConstants<float>::pi;
        return 0.5f - 0.5f * std::cos(twoPi * phase01In);
    }

    void incrementWrite()
    {
        ++writeIndex;
        if (writeIndex >= static_cast<int>(buffer.size()))
        {
            writeIndex = 0;
        }
    }

    float wrapReadIndex(float index) const
    {
        const float size = static_cast<float>(buffer.size());

        float out = index;

        while (out < 0.0f)
        {
            out += size;
        }

        while (out >= size)
        {
            out -= size;
        }

        return out;
    }

    float readLinear(float readIndexFloat) const
    {
        const int size = static_cast<int>(buffer.size());

        const float wrapped = wrapReadIndex(readIndexFloat);

        const int index0 = static_cast<int>(std::floor(wrapped));
        const int index1 = (index0 + 1) % size;

        const float fraction = wrapped - static_cast<float>(index0);

        const float sample0 = buffer[static_cast<size_t>(index0)];
        const float sample1 = buffer[static_cast<size_t>(index1)];

        return sample0 + (sample1 - sample0) * fraction;
    }

    void advanceHeadPhaseAndReseedIfNeeded(ReadHead& readHead)
    {
        const float phaseIncrement = (1.0f / static_cast<float>(grainLengthSamples));
        readHead.Phase01 += phaseIncrement;

        if (readHead.Phase01 >= 1.0f)
        {
            readHead.Phase01 -= 1.0f;
            reseedHead(readHead, 0.0f);
        }
    }

    void reseedHead(ReadHead& readHead, float offsetSamples)
    {
        const float safeBehindSamples = static_cast<float>(grainLengthSamples) * 2.0f;
        const float writeIndexFloat = static_cast<float>(writeIndex);
        readHead.ReadIndex = wrapReadIndex(writeIndexFloat - safeBehindSamples + offsetSamples);
    }

    static float windowFadeInOut(float phase01)
    {
        // phase01 in [0..1].
        // Use equal-power style:
        // - first half fades in
        // - second half fades out
        const float clamped = juce::jlimit(0.0f, 1.0f, phase01);

        if (clamped < 0.5f)
        {
            const float t = clamped * 2.0f; // 0..1
            const float s = std::sin(0.5f * juce::MathConstants<float>::pi * t);
            return s * s;
        }
        else
        {
            const float t = (clamped - 0.5f) * 2.0f; // 0..1
            const float c = std::cos(0.5f * juce::MathConstants<float>::pi * t);
            return c * c;
        }
    }

    static float windowFadeInEqualPower(float phase01)
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, phase01);
        const float s = std::sin(0.5f * juce::MathConstants<float>::pi * clamped);
        return s * s;
    }

    static float windowFadeOutEqualPower(float phase01)
    {
        const float clamped = juce::jlimit(0.0f, 1.0f, phase01);
        const float c = std::cos(0.5f * juce::MathConstants<float>::pi * clamped);
        return c * c;
    }

    double sampleRate = 48000.0;

    std::vector<float> buffer;
    int writeIndex = 0;

    int grainLengthSamples = 1440; // ~30 ms @ 48k

    ReadHead headA;
    ReadHead headB;

    float lastPitchRatio = 1.0f;
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
        //auto constantSequence = std::make_unique<ConstantRatioSequence>();
        //constantSequence->SetPitchRatio(2.0f); // +1 octave (very obvious)

        auto Backend = std::make_unique<GranularPitchBackend>();
        Backend->SetGrainLengthMilliseconds(35.0f);

        SetSequence(std::make_unique<ProgressiveOctaveSequence>());
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