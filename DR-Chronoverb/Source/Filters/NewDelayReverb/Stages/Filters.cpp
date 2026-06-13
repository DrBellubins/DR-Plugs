#include "Filters.h"

void Filters::PrepareToPlay(double newSampleRate)
{
    sampleRate = newSampleRate;

    // Prepare IIR filters
    juce::dsp::ProcessSpec filterSpec {};
    filterSpec.sampleRate = sampleRate;
    filterSpec.maximumBlockSize = 4096;
    filterSpec.numChannels = 1;

    lowpassL.prepare(filterSpec);
    lowpassR.prepare(filterSpec);
    highpassL.prepare(filterSpec);
    highpassR.prepare(filterSpec);

    updateFilters();
    filterRebuildPending.store(false, std::memory_order_release);
}

void Filters::ProcessBlock(juce::AudioBuffer<float>& audioBuffer)
{
    if (filterRebuildPending.exchange(false, std::memory_order_acq_rel))
        updateFilters();
}

std::pair<float, float> Filters::ProcessSample(float inputL, float inputR)
{
    float outputLeft = inputL;
    float outputRight = inputR;

    outputLeft = highpassL.processSample(outputLeft);
    outputRight = highpassR.processSample(outputRight);

    outputLeft = lowpassL.processSample(outputLeft);
    outputRight = lowpassR.processSample(outputRight);

    return std::make_pair(outputLeft, outputRight);
}

void Filters::updateFilters()
{
    auto lpCoeffs =
        juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate,  lowPassCutoff);

    auto hpCoeffs =
        juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, highPassCutoff);

    *lowpassL.coefficients = *lpCoeffs;
    *lowpassR.coefficients = *lpCoeffs;
    *highpassL.coefficients = *hpCoeffs;
    *highpassR.coefficients = *hpCoeffs;
}

void Filters::SetLowPassCutoff(float cutoff)
{
    lowPassCutoff = cutoff;
    filterRebuildPending.store(true, std::memory_order_release);
}

void Filters::SetHighPassCutoff(float cutoff)
{
    highPassCutoff = cutoff;
    filterRebuildPending.store(true, std::memory_order_release);
}