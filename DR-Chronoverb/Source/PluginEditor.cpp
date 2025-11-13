#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    createKnob(dryWetMixKnob, dryWetMixAttachment, "dryWetMix", "", 80, 300, 50);

    // Quality slider
    createSlider(diffusionQualitySlider, diffusionQualityAttachment, "diffusionQuality", 200, 20, 200, -180);
    createSliderLabel(diffusionQualityLabel, *diffusionQualitySlider, "Diffusion Quality", 15.0f, 170);

    // ------ Knob Labels ------
    createKnobLabel(delayTimeLabel, *delayTimeKnob, "Delay", 20.0f, 110);
    createKnobLabel(feedbackLabel, *feedbackTimeKnob, "Feedback", 15.0f, 70);
    createKnobLabel(diffusionAmountLabel, *diffusionAmountKnob, "Diffusion Amount", 15.0f, 70);
    createKnobLabel(diffusionSizeLabel, *diffusionSizeKnob, "Diffusion Size", 15.0f, 70);
    createKnobLabel(dryWetMixLabel, *dryWetMixKnob, "Dry/Wet Mix", 15.0f, 70);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

void AudioPluginAudioProcessorEditor::createSlider(std::unique_ptr<ThemedSlider>& slider,
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment, juce::String paramID,
        int width, int height, int offsetFromCenterX, int offsetFromCenterY)
{
    slider = std::make_unique<ThemedSlider>(
        "", nullptr, nullptr, "", juce::Slider::NoTextBox);

    slider->setLookAndFeel(&flatKnobLAF);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, paramID, *slider);

    addAndMakeVisible(*slider);

    int sliderX = (getWidth() / 2) - (width / 2) + offsetFromCenterX;
    int sliderY = (getHeight() / 2) - (height / 2) + offsetFromCenterY;

    slider->setBounds(sliderX, sliderY, width, height);
}

void AudioPluginAudioProcessorEditor::createSliderLabel(std::unique_ptr<juce::Label>& label, ThemedSlider& slider,
        juce::String text, float fontSize, int offsetX)
{
    label = std::make_unique<juce::Label>();
    label->setText(text, juce::dontSendNotification);

    juce::Font MainFont("Liberation Sans", fontSize, juce::Font::bold);
    MainFont.setExtraKerningFactor(0.05f);

    label->setFont(MainFont);
    label->setJustificationType(juce::Justification::centred);

    addAndMakeVisible(*label);

    int labelWidth = getLabelWidth(label);

    juce::Rectangle<int> sliderBounds = slider.getBounds();
    int labelX = sliderBounds.getCentreX() - (labelWidth / 2) - offsetX;
    int labelY = sliderBounds.getCentreY() - (sliderBounds.getHeight() / 2);

    label->setBounds(labelX, labelY, labelWidth, 20);
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

    juce::Rectangle<int> knobBounds = knob.getBounds();
    int labelX = knobBounds.getCentreX() - (labelWidth / 2);
    int labelY = knobBounds.getCentreY() - offsetY;

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

    if (background.isValid())
        graphics.drawImageAt(background, 0, 0);

    if (logo.isValid())
        graphics.drawImage(logo, juce::Rectangle<float>(0, 0, 256.0f, 60.0f), juce::RectanglePlacement::centred);

    // Draw bounding box for this component
    graphics.setColour(juce::Colours::red);
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
    }
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor.
}
