#pragma once

#include <atomic>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

// ImpulseClickSynth
// - Produces a short "click" burst when Trigger() is called.
// - Designed to be used like ComputerKeyboardSquareSynth: call PrepareToPlay(), then Process(buffer).
// - Thread-safe trigger flag (UI thread -> audio thread).
class ImpulseClickSynth
{
public:
    void PrepareToPlay(double newSampleRate)
    {
        sampleRate = (newSampleRate > 0.0 ? newSampleRate : 48000.0);
        resetState();
    }

    // UI thread safe: request a click.
    void Trigger()
    {
        pendingTrigger.store(true, std::memory_order_release);
    }

    // Optional: if you want "hold = repeated clicking" later, keep this.
    void SetHeld(bool shouldBeHeld)
    {
        held.store(shouldBeHeld, std::memory_order_release);
    }

    void Process(juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (numChannels <= 0 || numSamples <= 0)
            return;

        // Convert a UI trigger into an active click on the audio thread.
        if (pendingTrigger.exchange(false, std::memory_order_acq_rel))
            startClick();

        // (Optional) if held is enabled, you could retrigger periodically.
        juce::ignoreUnused(numChannels);

        if (remainingSamples <= 0)
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            if (remainingSamples <= 0)
                break;

            const float sample = renderOneSample();

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addSample(ch, i, sample);

            --remainingSamples;
        }
    }

private:
    void resetState()
    {
        remainingSamples = 0;
        phase = 0.0f;
        pendingTrigger.store(false, std::memory_order_release);
        held.store(false, std::memory_order_release);
    }

    void startClick()
    {
        // Short, bright click: decaying sine "tick"
        // You can tune these constants later.
        remainingSamples = static_cast<int>(std::round(clickLengthMs * 0.001 * sampleRate));
        remainingSamples = std::max(1, remainingSamples);

        phase = 0.0f;
    }

    float renderOneSample()
    {
        // Exponential decay envelope over the click duration.
        const int totalSamples = std::max(1, static_cast<int>(std::round(clickLengthMs * 0.001 * sampleRate)));
        const float t = 1.0f - (static_cast<float>(remainingSamples) / static_cast<float>(totalSamples));
        const float env = std::exp(-decay * t);

        phase += (2.0f * juce::MathConstants<float>::pi * clickHz) / static_cast<float>(sampleRate);
        if (phase > 2.0f * juce::MathConstants<float>::pi)
            phase -= 2.0f * juce::MathConstants<float>::pi;

        const float s = std::sin(phase);

        return (s * env) * clickGain;
    }

private:
    double sampleRate = 48000.0;

    std::atomic<bool> pendingTrigger { false };
    std::atomic<bool> held { false };

    int remainingSamples = 0;
    float phase = 0.0f;

    // Tunables
    float clickGain = 0.25f;     // overall loudness
    float clickHz = 3500.0f;     // brightness
    float clickLengthMs = 6.0f;  // duration
    float decay = 7.0f;          // envelope decay curve
};