#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/Theme.h"

static FlatRotaryLookAndFeel flatKnobLAF;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll(BGGray);

    delayTimeKnob = std::make_unique<ThemedKnob>(
        "Arp Rate", nullptr, nullptr, " Rate", juce::Slider::NoTextBox);

    delayTimeKnob->setTextValueSuffix(" Rate");
    delayTimeKnob->setLookAndFeel(&flatKnobLAF);

    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "delayTime", *delayTimeKnob);

    addAndMakeVisible(*delayTimeKnob);

    int delayTimeWidthHeight = 150;
    int delayTimeX = (getWidth() / 2) - (delayTimeWidthHeight / 2);
    int delayTimeY = (getHeight() / 2) - (delayTimeWidthHeight / 2);

    delayTimeKnob->setBounds(delayTimeX, delayTimeY - 25, delayTimeWidthHeight, delayTimeWidthHeight);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor.
}
