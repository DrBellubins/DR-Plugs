#include "PluginProcessor.h"
#include "PluginEditor.h"

// TODO: Derive class from RoundedToggle to make Pre/Post toggles with labels
// TODO: Begin creation of Matrix Menu.
// TODO: Implement pitch features (oh fuck).
// TODO: Implement playback direction features.

class DebugPage : public juce::Component
{
public:
    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(juce::Colours::yellow.withAlpha(0.2f));
        graphics.fillRect(getLocalBounds());

        graphics.setColour(juce::Colours::yellow);
        graphics.drawRect(getLocalBounds(), 2);
    }
};

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor(&processor),
      processorRef(processor),
      uiHelpers(processorRef.parameters, flatKnobLAF)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(880, 580);

    const int cX = getWidth() / 2;
    const int cY = getHeight() / 2;

    setWantsKeyboardFocus(true);
    addKeyListener(this);

    // Background
    //background = juce::ImageFileFormat::loadFrom(BinaryData::bg_png, BinaryData::bg_pngSize);

    // Logo
    logo = juce::ImageFileFormat::loadFrom(BinaryData::logo_png, BinaryData::logo_pngSize);

    // ------ KNOBS ------
    uiHelpers.CreateKnob(*this, delayTimeKnob, delayTimeAttachment, "delayTime",
        " ms", 100, cX + 0, cY + -25 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, feedbackTimeKnob, feedbackTimeAttachment, "feedbackTime",
        "", 80, cX + 200, cY + 50 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, diffusionAmountKnob, diffusionAmountAttachment, "diffusionAmount",
        "", 80, cX + -350, cY + -125 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, diffusionSizeKnob, diffusionSizeAttachment, "diffusionSize",
        "", 80, cX + -200, cY + -125 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, dryWetMixKnob, dryWetMixAttachment, "dryWetMix",
        "", 80, cX + 350, cY + 50 + nonPitchYOffset);

    // Filters
    uiHelpers.CreateKnob(*this, stereoSpreadKnob, stereoSpreadAttachment, "stereoSpread",
        "", 80, cX + 200, cY + -125 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, lowPassKnob, lowPassAttachment, "lowPassCutoff",
        "", 80, cX + -350, cY + 50 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, highPassKnob, highPassAttachment, "highPassCutoff",
        "", 80, cX + -200, cY + 50 + nonPitchYOffset);

    // Ducking
    uiHelpers.CreateKnob(*this, duckAmountKnob, duckAmountAttachment, "duckAmount",
        "", 60, cX + 0, cY + -170 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, duckAttackKnob, duckAttackAttachment, "duckAttack",
        "", 60, cX + -80, cY + -170 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, duckReleaseKnob, duckReleaseAttachment, "duckRelease",
        "", 60, cX + 80, cY + -170 + nonPitchYOffset);

    // Quality slider
    uiHelpers.CreateSlider(*this, diffusionQualitySlider, diffusionQualityAttachment, "diffusionQuality",
        200, 20, cX + 200, cY + -260);

    uiHelpers.CreateSliderLabel(*this, diffusionQualityLabel, *diffusionQualitySlider,
        "Diffusion Quality", 15.0f, 170);

    /*if (diffusionQualitySlider != nullptr)
    {
        diffusionQualitySlider->onValueChange = [this]()
        {
            auto* qualityParam = processorRef.parameters.getParameter("diffusionQuality");

            if (qualityParam != nullptr)
            {
                int qualityRounded = static_cast<int>(std::round(ModeParameter->convertFrom0to1(ModeParameter->getValue())));
            }
        };
    }*/

    // ------ Knob Labels ------
    uiHelpers.CreateKnobLabel(*this, delayTimeLabel, *delayTimeKnob, "Delay Time", 20.0f, 80);
    uiHelpers.CreateKnobLabel(*this, feedbackLabel, *feedbackTimeKnob, "Feedback", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, diffusionAmountLabel, *diffusionAmountKnob,
        "Diffusion Amount", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, diffusionSizeLabel, *diffusionSizeKnob,
        "Diffusion Size", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, dryWetMixLabel, *dryWetMixKnob, "Dry/Wet Mix", 15.0f, 70);

    // Filters
    uiHelpers.CreateKnobLabel(*this, stereoSpreadLabel, *stereoSpreadKnob,
        "Stereo Spread", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, lowPassLabel, *lowPassKnob, "Low Pass", 15.0f, 70);
    uiHelpers.CreateKnobLabel(*this, highPassLabel, *highPassKnob, "High Pass", 15.0f, 70);

    // Ducking
    uiHelpers.CreateKnobLabel(*this, duckAmountLabel, *duckAmountKnob, "Duck", 15.0f, 50);
    uiHelpers.CreateKnobLabel(*this, duckAttackLabel, *duckAttackKnob, "Attack", 15.0f, 50);
    uiHelpers.CreateKnobLabel(*this, duckReleaseLabel, *duckReleaseKnob, "Release", 15.0f, 50);

    // Delay mode
    delayTimeModeButtons = std::make_unique<SegmentedButton>(juce::StringArray{ "ms", "nrm", "trip", "dot" });

    addAndMakeVisible(*delayTimeModeButtons);
    delayTimeModeButtons->setBounds((getWidth() / 2) - 100, (getHeight() / 2) + 50 + nonPitchYOffset, 200, 30);

    delayTimeModeAttachment = std::make_unique<SegmentedButton::ChoiceAttachment>(processorRef.parameters, "delayMode", *delayTimeModeButtons);

    // Snap knob immediately when mode changes into a synced mode.
    delayTimeModeButtons->onSelectionChanged = [this](int NewIndex)
    {
        if (NewIndex != 0)
            snapDelayKnobToNearestStep();
    };

    // While dragging in a sync mode, keep snapping so the knob visually “steps.”
    if (delayTimeKnob != nullptr)
    {
        delayTimeKnob->onValueChange = [this]()
        {
            // Only snap in beat-synced modes
            auto* ModeParameter = processorRef.parameters.getParameter("delayMode");

            if (ModeParameter != nullptr)
            {
                int ModeIndex = static_cast<int>(std::round(ModeParameter->convertFrom0to1(ModeParameter->getValue())));

                if (ModeIndex != 0)
                    snapDelayKnobToNearestStep();
            }
        };
    }

    // ------ Toggles ------
    uiHelpers.CreateToggle(*this, hplpFilterToggle,
        hplpFilterToggleAttachment,
        RoundedToggle::Orientation::Vertical,
        "hplpPrePost",
        20, 50, cX + -275, cY + 50 + nonPitchYOffset);


    // ------ TABBED PAGE BOX ------

    bottomTabbedPageBox = std::make_unique<TabbedPageBox>();
    addAndMakeVisible(*bottomTabbedPageBox);
    bottomTabbedPageBox->setBounds(25, 430, 830, 140);

    pitchPage = std::make_unique<Component>();
    distortionPage = std::make_unique<Component>();
    tapePage = std::make_unique<Component>();
    granularPage = std::make_unique<Component>();

    bottomTabbedPageBox->AddTab("Pitch", pitchPage.get());
    bottomTabbedPageBox->AddTab("Distortion", distortionPage.get());
    bottomTabbedPageBox->AddTab("Tape", tapePage.get());
    bottomTabbedPageBox->AddTab("Granular", granularPage.get());

    uiHelpers.CreateCheckbox(*pitchPage, pitchShiftToggle,
        pitchShiftToggleAttachment,
        "pitchShiftEnabled",
        20, 20, 30, 30);

    uiHelpers.CreateCheckboxLabel(*pitchPage, pitchShiftTitle, *pitchShiftToggle, "", 14.0f, -40);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
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
            UpperKey = static_cast<int>(KeyCode - 'a' + 'A');
        else if (KeyCode >= 'A' && KeyCode <= 'Z')
            LowerKey = static_cast<int>(KeyCode - 'A' + 'a');

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
            processorRef.keyboardSynth.HandleKeyChange(KeyCode, true);
    }

    // Released
    for (int KeyCode : lastHeldKeyCodes)
    {
        if (currentHeld.find(KeyCode) == currentHeld.end())
            processorRef.keyboardSynth.HandleKeyChange(KeyCode, false);
    }

    lastHeldKeyCodes.swap(currentHeld);
    return true; // Consume
}

void AudioPluginAudioProcessorEditor::snapDelayKnobToNearestStep()
{
    if (delayTimeKnob == nullptr)
        return;

    auto* ModeParameter = processorRef.parameters.getParameter("delayMode");

    if (ModeParameter == nullptr)
        return;

    int ModeIndex = static_cast<int>(std::round(ModeParameter->convertFrom0to1(ModeParameter->getValue())));

    // Only quantize for beat-synced modes
    if (ModeIndex == 0)
        return;

    float CurrentValue = delayTimeKnob->getValue();

    // Find nearest discrete normalized position
    float Nearest = DelaySyncNormalizedPositions[0];
    float SmallestDistance = std::abs(CurrentValue - Nearest);

    for (float StepValue : DelaySyncNormalizedPositions)
    {
        float Distance = std::abs(CurrentValue - StepValue);

        if (Distance < SmallestDistance)
        {
            SmallestDistance = Distance;
            Nearest = StepValue;
        }
    }

    // Avoid churn: only update if meaningfully different
    if (std::abs(CurrentValue - Nearest) > 0.0005f)
    {
        // Use dontSendNotification so we do not recursively trigger the value change handler again.
        // SliderAttachment will still see the changed value and push it to the parameter.
        delayTimeKnob->setValue(Nearest, juce::dontSendNotification);
    }
}
