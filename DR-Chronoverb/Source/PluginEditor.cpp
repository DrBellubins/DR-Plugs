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

    // ------ KNOBS ------
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

    // ------ Labels ------
    createLabel(delayTimeLabel, "Delay");

    int delayTimeLabelWidth = getLabelWidth(delayTimeLabel);

    juce::Rectangle<int> delayTimeKnobBounds = delayTimeKnob->getBounds();
    int delayTimeLabelX = delayTimeKnobBounds.getCentreX() - (delayTimeLabelWidth / 2);
    int delayTimeLabelY = delayTimeKnobBounds.getCentreY() - 110;

    delayTimeLabel->setBounds(delayTimeLabelX, delayTimeLabelY, delayTimeLabelWidth, 20);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

void AudioPluginAudioProcessorEditor::createLabel(std::unique_ptr<juce::Label>& label, juce::String Text)
{
    label = std::make_unique<juce::Label>();
    label->setText(Text, juce::dontSendNotification);

    juce::Font font = juce::Font(20.0f);

    font.setTypefaceName("");

    label->setFont(juce::Font(20.0f));
    label->setJustificationType(juce::Justification::centred);

    addAndMakeVisible(*label);
}

int AudioPluginAudioProcessorEditor::getLabelWidth(std::unique_ptr<juce::Label>& label)
{
    return label->getFont().getStringWidth(label->getText());
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll(BGGray);

    // Draw bounding box for this component
    /*graphics.setColour(juce::Colours::red);
    graphics.drawRect(getLocalBounds(), 2);

    // Draw bounding boxes for children
    for (int ChildIndex = 0; ChildIndex < getNumChildComponents(); ++ChildIndex)
    {
        auto* Child = getChildComponent(ChildIndex);

        if (Child != nullptr)
        {
            juce::Rectangle<int> ChildBounds = Child->getBounds();
            graphics.setColour(juce::Colours::green);
            graphics.drawRect(ChildBounds, 2);
        }
    }*/
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor.
}
