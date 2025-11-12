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
    createKnob(delayTimeKnob, "delayTime", 150, 0, -25);
    createKnob(feedbackTimeKnob, "feedbackTime", 80, 300, -25);
    createKnob(diffusionAmountKnob, "diffusionAmount", 80, -300, -100);

    // ------ Labels ------
    createKnobLabel(delayTimeLabel, delayTimeKnob, "Delay");
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

void AudioPluginAudioProcessorEditor::createKnob(std::unique_ptr<ThemedKnob>& knob, juce::String paramID,
        int widthHeight, int offsetFromCenterX, int offsetFromCenterY)
{
    knob = std::make_unique<ThemedKnob>(
        "Delay", nullptr, nullptr, " Rate", juce::Slider::NoTextBox);

    knob->setLookAndFeel(&flatKnobLAF);

    delayTimeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, paramID, *knob);

    addAndMakeVisible(*knob);

    int knobX = (getWidth() / 2) - (widthHeight / 2) + offsetFromCenterX;
    int knobY = (getHeight() / 2) - (widthHeight / 2) + offsetFromCenterY;

    knob->setBounds(knobX, knobY, widthHeight, widthHeight);
}

void AudioPluginAudioProcessorEditor::createKnobLabel(std::unique_ptr<juce::Label>& label, std::unique_ptr<ThemedKnob> knob, juce::String text)
{
    label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);

    juce::Font MainFont("Liberation Sans", 20.0f, juce::Font::bold);
    MainFont.setExtraKerningFactor(0.05f);

    label->setFont(MainFont);
    label->setJustificationType(juce::Justification::centred);

    addAndMakeVisible(*label);

    int labelWidth = getLabelWidth(delayTimeLabel);

    juce::Rectangle<int> delayTimeKnobBounds = knob->getBounds();
    int labelX = delayTimeKnobBounds.getCentreX() - (labelWidth / 2);
    int labelY = delayTimeKnobBounds.getCentreY() - 110;

    label->setBounds(labelX, labelY, labelWidth, 20);
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
