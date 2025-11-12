#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/Theme.h"
#include "Utils/ThemedKnob.h"

static FlatRotaryLookAndFeel flatKnobLAF;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(800, 400);

    // Delay time
    delayTimeKnob = std::make_unique<ThemedKnob>(
        "Delay", nullptr, nullptr, " Rate", juce::Slider::NoTextBox);

    delayTimeKnob->setLookAndFeel(&flatKnobLAF);

    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "delayTime", *delayTimeKnob);

    addAndMakeVisible(*delayTimeKnob);

    int delayTimeWidthHeight = 150;
    int delayTimeX = (getWidth() / 2) - (delayTimeWidthHeight / 2);
    int delayTimeY = (getHeight() / 2) - (delayTimeWidthHeight / 2) - 25;

    delayTimeKnob->setBounds(delayTimeX, delayTimeY, delayTimeWidthHeight, delayTimeWidthHeight);

    auto* delayTimeLabel = new LabelAttachment("My Label", delayTimeKnob.get(), LabelAttachment::Position::Above);
    addAndMakeVisible(*delayTimeLabel);

    // Feedback time
    feedbackTimeKnob = std::make_unique<ThemedKnob>(
        "Feedback", nullptr, nullptr, " Rate", juce::Slider::NoTextBox);

    feedbackTimeKnob->setLookAndFeel(&flatKnobLAF);

    feedbackTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "feedbackTime", *feedbackTimeKnob);

    addAndMakeVisible(*feedbackTimeKnob);

    int feedbackTimeWidthHeight = 80;
    int feedbackTimeX = (getWidth() / 2) - (feedbackTimeWidthHeight / 2) + 300;
    int feedbackTimeY = (getHeight() / 2) - (feedbackTimeWidthHeight / 2) - 25;

    feedbackTimeKnob->setBounds(feedbackTimeX, feedbackTimeY, feedbackTimeWidthHeight, feedbackTimeWidthHeight);

    // Diffusion amount
    diffusionAmountKnob = std::make_unique<ThemedKnob>(
        "Diffusion amount", nullptr, nullptr, " Rate", juce::Slider::NoTextBox);

    diffusionAmountKnob->setLookAndFeel(&flatKnobLAF);

    diffusionAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "diffusionAmount", *diffusionAmountKnob);

    addAndMakeVisible(*diffusionAmountKnob);

    int diffusionAmountWidthHeight = 80;
    int diffusionAmountX = (getWidth() / 2) - (diffusionAmountWidthHeight / 2) - 300;
    int diffusionAmountY = (getHeight() / 2) - (diffusionAmountWidthHeight / 2) - 100;

    diffusionAmountKnob->setBounds(diffusionAmountX, diffusionAmountY, diffusionAmountWidthHeight, diffusionAmountWidthHeight);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll(BGGray);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor.
}
