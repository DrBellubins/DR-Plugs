#pragma once

class IPitchSequence
{
public:
    virtual ~IPitchSequence() = default;

    virtual void Reset() {}
    virtual void AdvanceToNextEcho() {}

    // Pitch ratio for current echo (1.0 = unison, 2.0 = +1 octave, 0.5 = -1 octave).
    virtual float GetCurrentPitchRatio() const = 0;
};

class ConstantRatioSequence : public IPitchSequence
{
public:
    void SetPitchRatio(float r) { pitchRatio = r; }
    float GetCurrentPitchRatio() const override { return pitchRatio; }

private:
    float pitchRatio = 1.0f;
};

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

    // Called at each echo boundary — the only place ratio changes are permitted.
    // Both granular and phase vocoder backends must implement this.
    virtual void OnEchoBoundary(float newRatio) { juce::ignoreUnused(newRatio); }

    // Sets the initial ratio with no crossfade. Safe to call outside the audio thread.
    virtual void SetInitialRatio(float ratio) { juce::ignoreUnused(ratio); }

    virtual float GetLatencyMilliseconds() const { return 0.0f; }
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