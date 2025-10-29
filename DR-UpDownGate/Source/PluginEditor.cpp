#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Utils/Theme.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (300, 500);

    gateRangeSlider = std::make_unique<VerticalRangeSlider>(0.0f, 1.0f);

    addAndMakeVisible(*gateRangeSlider);

    gateRangeSlider->setBounds(150, 50, 100, 400); // Position as needed
    gateRangeSlider->setRoundness(10.0f);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() = default;

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll(BGGray);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor
}
