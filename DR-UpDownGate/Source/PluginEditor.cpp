#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);

    rangeSlider = std::make_unique<VerticalRangeSlider>(0.0f, 1.0f); // For example

    addAndMakeVisible(*rangeSlider);

    rangeSlider->setBounds(50, 50, 40, 200); // Position as needed
    rangeSlider->setRoundness(10.0f);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() = default;

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    /*graphics.setColour (juce::Colours::white);
    graphics.setFont (15.0f);
    graphics.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);*/
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor
}
