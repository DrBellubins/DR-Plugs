#pragma once

#include <utility>
#include <juce_dsp/juce_dsp.h>

class Filters
{
public:
    void PrepareToPlay(double newSampleRate);

    void ProcessBlock(juce::AudioBuffer<float>& audioBuffer);
    std::pair<float, float> ProcessSample(float inputL, float inputR);

    void SetLowPassCutoff(float cutoff);
    void SetHighPassCutoff(float cutoff);

private:
    void updateFilters();

    double sampleRate = 48000.0f;

    float lowPassCutoff = 1.0f;
    float highPassCutoff = 0.0f;

    std::atomic<bool> filterRebuildPending { false };

    juce::dsp::IIR::Filter<float> lowpassL;
    juce::dsp::IIR::Filter<float> lowpassR;

    juce::dsp::IIR::Filter<float> highpassL;
    juce::dsp::IIR::Filter<float> highpassR;
};
