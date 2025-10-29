#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Utils/Theme.h"
#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/EnvelopeKnob.h"

static FlatRotaryLookAndFeel flatKnobLAF;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (300, 500);

    // Range slider
    rangeSlider = std::make_unique<VerticalRangeSlider>(0.0f, 1.0f);

    addAndMakeVisible(*rangeSlider);

    rangeSlider->setBounds(150, 50, 100, 400); // Position as needed
    rangeSlider->setRoundness(10.0f);

    // Attack knob
    attackKnob = std::make_unique<EnvelopeKnob>("Attack");
    attackKnob->setLookAndFeel(&flatKnobLAF);
    attackKnob->setSliderStyle(juce::Slider::RotaryVerticalDrag);
    attackKnob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 20);
    attackKnob->setRange(0.0, 1.0, 0.01);
    attackKnob->setBounds(25, 100, 100, 100);

    addAndMakeVisible(*attackKnob);

    // Release knob
    /*releaseKnob = std::make_unique<juce::Slider>();
    releaseKnob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    releaseKnob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    releaseKnob->setRange(0.0, 1.0, 0.01);
    releaseKnob->setBounds(50, 50, 100, 100);

    addAndMakeVisible(*releaseKnob);*/
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() = default;

//==============================================================================c
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
