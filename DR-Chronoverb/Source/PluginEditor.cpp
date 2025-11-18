#include "PluginProcessor.h"
#include "PluginEditor.h"

static FlatRotaryLookAndFeel flatKnobLAF;

// TODO: Create fractional time buttons (ms, normal, triplet, dot)

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(880, 580);

    setWantsKeyboardFocus(true);
    addKeyListener(this);

    // Background
    //background = juce::ImageFileFormat::loadFrom(BinaryData::bg_png, BinaryData::bg_pngSize);

    // Logo
    logo = juce::ImageFileFormat::loadFrom(BinaryData::logo_png, BinaryData::logo_pngSize);

    // ------ KNOBS ------

    // TODO: delayTimeKnob needs to have its suffix value * 1000 for accurate ms display
    createKnob(delayTimeKnob, delayTimeAttachment, "delayTime", " ms", 150, 0, -25);
    createKnob(feedbackTimeKnob, feedbackTimeAttachment, "feedbackTime", "", 80, 200, 50);
    createKnob(diffusionAmountKnob, diffusionAmountAttachment, "diffusionAmount", "", 80, -350, -125);
    createKnob(diffusionSizeKnob, diffusionSizeAttachment, "diffusionSize", "", 80, -200, -125);
    createKnob(dryWetMixKnob, dryWetMixAttachment, "dryWetMix", "", 80, 350, 50);

    createKnob(stereoSpreadKnob, stereoSpreadAttachment, "stereoSpread", "", 80, 200, -125);
    createKnob(lowPassKnob, lowPassAttachment, "lowPassDecay", "", 80, -350, 50);
    createKnob(highPassKnob, highPassAttachment, "highPassDecay", "", 80, -200, 50);

    // Quality slider
    createSlider(diffusionQualitySlider, diffusionQualityAttachment, "diffusionQuality", 200, 20, 200, -260);
    createSliderLabel(diffusionQualityLabel, *diffusionQualitySlider, "Diffusion Quality", 15.0f, 170);

    // ------ Knob Labels ------
    createKnobLabel(delayTimeLabel, *delayTimeKnob, "Delay", 20.0f, 110);
    createKnobLabel(feedbackLabel, *feedbackTimeKnob, "Feedback", 15.0f, 70);
    createKnobLabel(diffusionAmountLabel, *diffusionAmountKnob, "Diffusion Amount", 15.0f, 70);
    createKnobLabel(diffusionSizeLabel, *diffusionSizeKnob, "Diffusion Size", 15.0f, 70);
    createKnobLabel(dryWetMixLabel, *dryWetMixKnob, "Dry/Wet Mix", 15.0f, 70);

    createKnobLabel(stereoSpreadLabel, *stereoSpreadKnob, "Stereo Spread", 15.0f, 70);
    createKnobLabel(lowPassLabel, *lowPassKnob, "Low Pass Decay", 15.0f, 70);
    createKnobLabel(highPassLabel, *highPassKnob, "High Pass Decay", 15.0f, 70);
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
        "", nullptr, nullptr, suffix, juce::Slider::NoTextBox);

    knob->setLookAndFeel(&flatKnobLAF);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, paramID, *knob);

    addAndMakeVisible(*knob);

    int knobX = (getWidth() / 2) - (widthHeight / 2) + offsetFromCenterX;
    int knobY = (getHeight() / 2) - (widthHeight / 2) + offsetFromCenterY;

    knob->setBounds(knobX, knobY, widthHeight, widthHeight);

    //knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 22);
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

    //if (background.isValid())
    //    graphics.drawImageAt(background, 0, 0);

    if (logo.isValid())
        graphics.drawImage(logo, juce::Rectangle<float>(-70, -15, 512.0f, 120.0f), juce::RectanglePlacement::centred);

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

// Forward keyboard input to the Keyboard Synth in the processor.
bool AudioPluginAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused(originatingComponent);

    int KeyCode = 0;

    // Prefer text character when available (letters), fallback to key code.
    if (key.getTextCharacter() != 0)
        KeyCode = static_cast<int>(key.getTextCharacter());
    else
        KeyCode = key.getKeyCode();

    processorRef.keyboardSynth.HandleKeyChange(KeyCode, true);
    lastHeldKeyCodes.insert(KeyCode);
    return true; // Consume
}

bool AudioPluginAudioProcessorEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
    juce::ignoreUnused(isKeyDown, originatingComponent);

    // Poll the mapped key set and compare against lastHeldKeyCodes to detect releases/presses.
    std::unordered_set<int> currentHeld;

    for (int KeyCode : processorRef.keyboardSynth.GetMappedKeyCodes())
    {
        // Check lower-case and upper-case variants
        int LowerKey = KeyCode;
        int UpperKey = KeyCode;

        if (KeyCode >= 'a' && KeyCode <= 'z')
        {
            UpperKey = static_cast<int>(KeyCode - 'a' + 'A');
        }
        else if (KeyCode >= 'A' && KeyCode <= 'Z')
        {
            LowerKey = static_cast<int>(KeyCode - 'A' + 'a');
        }

        if (juce::KeyPress::isKeyCurrentlyDown(KeyCode)
            || juce::KeyPress::isKeyCurrentlyDown(LowerKey)
            || juce::KeyPress::isKeyCurrentlyDown(UpperKey))
        {
            currentHeld.insert(KeyCode);
        }
    }

    // Newly pressed
    for (int KeyCode : currentHeld)
    {
        if (lastHeldKeyCodes.find(KeyCode) == lastHeldKeyCodes.end())
        {
            processorRef.keyboardSynth.HandleKeyChange(KeyCode, true);
        }
    }

    // Released
    for (int KeyCode : lastHeldKeyCodes)
    {
        if (currentHeld.find(KeyCode) == currentHeld.end())
        {
            processorRef.keyboardSynth.HandleKeyChange(KeyCode, false);
        }
    }

    lastHeldKeyCodes.swap(currentHeld);
    return true; // Consume
}
