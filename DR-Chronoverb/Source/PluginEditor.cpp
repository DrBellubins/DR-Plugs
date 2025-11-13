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

    // Background
    background = juce::ImageFileFormat::loadFrom(BinaryData::bg_png, BinaryData::bg_pngSize);

    // Logo
    logo = juce::ImageFileFormat::loadFrom(BinaryData::logo_png, BinaryData::logo_pngSize);

    // ------ KNOBS ------

    // TODO: delayTimeKnob needs to have its suffix value * 1000 for accurate ms display
    createKnob(delayTimeKnob, delayTimeAttachment, "delayTime", " ms", 150, 0, -25);
    createKnob(feedbackTimeKnob, feedbackTimeAttachment, "feedbackTime", "", 80, 150, 50);
    createKnob(diffusionAmountKnob, diffusionAmountAttachment, "diffusionAmount", "", 80, -300, -50);
    createKnob(diffusionSizeKnob, diffusionSizeAttachment, "diffusionSize", "", 80, -150, -50);

    // ------ Labels ------
    createKnobLabel(delayTimeLabel, *delayTimeKnob, "Delay", 20.0f, 110);
    createKnobLabel(feedbackLabel, *feedbackTimeKnob, "Feedback", 15.0f, 70);
    createKnobLabel(diffusionAmountLabel, *diffusionAmountKnob, "Diffusion Amount", 15.0f, 70);
    createKnobLabel(diffusionSizeLabel, *diffusionSizeKnob, "Diffusion Size", 15.0f, 70);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

void AudioPluginAudioProcessorEditor::createKnob(std::unique_ptr<ThemedKnob>& knob, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
    juce::String paramID, juce::String suffix, int widthHeight, int offsetFromCenterX, int offsetFromCenterY)
{
    knob = std::make_unique<ThemedKnob>(
        "", nullptr, nullptr, suffix, juce::Slider::TextBoxBelow);

    knob->setLookAndFeel(&flatKnobLAF);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, paramID, *knob);

    addAndMakeVisible(*knob);

    int knobX = (getWidth() / 2) - (widthHeight / 2) + offsetFromCenterX;
    int knobY = (getHeight() / 2) - (widthHeight / 2) + offsetFromCenterY;

    knob->setBounds(knobX, knobY, widthHeight, widthHeight);
}

void AudioPluginAudioProcessorEditor::createKnobLabel(std::unique_ptr<juce::Label>& label,
    ThemedKnob& knob, juce::String text, float fontSize, int offsetY)
{
    label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);

    juce::Font MainFont("Liberation Sans", fontSize, juce::Font::bold);
    MainFont.setExtraKerningFactor(0.05f);

    label->setFont(MainFont);
    label->setJustificationType(juce::Justification::centred);

    addAndMakeVisible(*label);

    int labelWidth = getLabelWidth(label);

    juce::Rectangle<int> delayTimeKnobBounds = knob.getBounds();
    int labelX = delayTimeKnobBounds.getCentreX() - (labelWidth / 2);
    int labelY = delayTimeKnobBounds.getCentreY() - offsetY;

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

    if (logo.isValid())
    {
        graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        graphics.drawImage(logo, juce::Rectangle<float>(0, 0, 256.0f, 60.0f), juce::RectanglePlacement::centred);

    }

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
