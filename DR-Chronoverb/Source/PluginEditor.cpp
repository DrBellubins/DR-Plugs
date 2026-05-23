#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    setSize(900, 650);

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
        "", 140, cX + 0, cY +  nonPitchYOffset);

    uiHelpers.CreateKnob(*this, feedbackTimeKnob, feedbackTimeAttachment, "feedbackTime",
        "", 100, cX + 350, cY + -125 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, diffusionAmountKnob, diffusionAmountAttachment, "diffusionAmount",
        "", 100, cX + -350, cY + -125 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, diffusionSizeKnob, diffusionSizeAttachment, "diffusionSize",
        "", 100, cX + -200, cY + -125 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, dryVolumeKnob, dryVolumeAttachment, "dryVolume",
        "", 100, cX + 200, cY + 50 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, wetVolumeKnob, wetVolumeAttachment, "wetVolume",
        "", 100, cX + 350, cY + 50 + nonPitchYOffset);

    // Filters
    uiHelpers.CreateKnob(*this, stereoSpreadKnob, stereoSpreadAttachment, "stereoSpread",
        "", 100, cX + 200, cY + -125 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, lowPassKnob, lowPassAttachment, "lowPassCutoff",
        "", 100, cX + -350, cY + 50 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, highPassKnob, highPassAttachment, "highPassCutoff",
        "", 100, cX + -200, cY + 50 + nonPitchYOffset);

    // Ducking
    uiHelpers.CreateKnob(*this, duckAmountKnob, duckAmountAttachment, "duckAmount",
        "", 70, cX + 0, cY + -170 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, duckAttackKnob, duckAttackAttachment, "duckAttack",
        "", 70, cX + -80, cY + -170 + nonPitchYOffset);

    uiHelpers.CreateKnob(*this, duckReleaseKnob, duckReleaseAttachment, "duckRelease",
        "", 70, cX + 80, cY + -170 + nonPitchYOffset);

    // Quality slider
    uiHelpers.CreateSlider(*this, diffusionQualitySlider, diffusionQualityAttachment, "diffusionQuality",
        200, 20, cX + 200, 30);

    uiHelpers.CreateSliderLabel(*this, diffusionQualityLabel, *diffusionQualitySlider,
        "Diffusion Quality", 15.0f, 170);

    // ------ Knob Labels ------
    uiHelpers.CreateKnobLabel(*this, delayTimeLabel, *delayTimeKnob, "Delay Time", 20.0f, 100);
    uiHelpers.CreateKnobLabel(*this, feedbackLabel, *feedbackTimeKnob, "Feedback", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, diffusionAmountLabel, *diffusionAmountKnob,
        "Diffusion Amount", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, diffusionSizeLabel, *diffusionSizeKnob,
        "Diffusion Size", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, dryVolumeLabel, *dryVolumeKnob,
        "Dry Volume", 15.0f, 70);

    uiHelpers.CreateKnobLabel(*this, wetVolumeLabel, *wetVolumeKnob,
        "Wet Volume", 15.0f, 70);

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
    delayTimeModeButtons->setBounds(cX - 100, cY + 85 + nonPitchYOffset, 200, 30);

    delayTimeModeAttachment = std::make_unique<SegmentedButton::ChoiceAttachment>(processorRef.parameters,
        "delayTimeMode", *delayTimeModeButtons);

    // Safe to call now — delayTimeModeButtons is fully constructed and the
    // attachment has already synced the selected index from the parameter.
    {
        const int InitialMode = delayTimeModeButtons->getSelectedIndex();
        lastDelayModeIndex = (InitialMode >= 0 ? InitialMode : 0);
        updateDelayKnobDisplay(lastDelayModeIndex);
    }

    // Snap knob immediately when mode changes into a synced mode.
    delayTimeModeButtons->onSelectionChanged = [this](int NewIndex)
    {
        // Save current knob value for the mode we're leaving.
        if (delayTimeKnob != nullptr)
            perModeLastDelayValue[lastDelayModeIndex] = static_cast<float>(delayTimeKnob->getValue());

        lastDelayModeIndex = NewIndex;

        // Restore the value last used in the arriving mode.
        float TargetValue = perModeLastDelayValue[NewIndex];

        // Snap to the nearest step when entering any beat-synced mode.
        if (NewIndex != 0)
        {
            float Nearest = DelaySyncNormalizedPositions[0];
            float SmallestDistance = std::abs(TargetValue - Nearest);

            for (float StepValue : DelaySyncNormalizedPositions)
            {
                const float Distance = std::abs(TargetValue - StepValue);

                if (Distance < SmallestDistance)
                {
                    SmallestDistance = Distance;
                    Nearest = StepValue;
                }
            }

            TargetValue = Nearest;
        }

        // Push the restored value into the parameter.
        if (auto* DelayParam = processorRef.parameters.getParameter("delayTime"))
            DelayParam->setValueNotifyingHost(TargetValue);

        // Update the knob text display format for the new mode.
        updateDelayKnobDisplay(NewIndex);
    };

    // While dragging in a sync mode, keep snapping so the knob visually “steps.”
    if (delayTimeKnob != nullptr)
    {
        delayTimeKnob->onValueChange = [this]()
        {
            // Only snap in beat-synced modes
            auto* ModeParameter = processorRef.parameters.getParameter("delayTimeMode");

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
    bottomTabbedPageBox->setBounds(25, 450, 850, 180);

    pitchPage = std::make_unique<Component>();
    distortionPage = std::make_unique<Component>();
    tapePage = std::make_unique<Component>();
    granularPage = std::make_unique<Component>();

    bottomTabbedPageBox->AddTab("Pitch", pitchPage.get());
    bottomTabbedPageBox->AddTab("Distortion", distortionPage.get());
    bottomTabbedPageBox->AddTab("Tape", tapePage.get());
    bottomTabbedPageBox->AddTab("Granular", granularPage.get());

    // Pitch shifting
    uiHelpers.CreateCheckbox(*pitchPage, pitchShiftStereoToggle,
    pitchShiftStereoToggleAttachment,
    "pitchStereoEnabled",
    20, 20, 30, 60);

    uiHelpers.CreateCheckboxLabel(*pitchPage, pitchShiftStereoLabel, *pitchShiftStereoToggle,
        "Stereo", 14.0f, -38);

    // Horizontal slider
    horizontalPitchRangeSlider = std::make_unique<HorizontalRangeSlider>(-48.0f, 48.0f);
    horizontalPitchRangeSlider->setMinimumRange(0.0f);
    horizontalPitchRangeSlider->setSteppingEnabled(true);
    horizontalPitchRangeSlider->setStepSize(12.0f);
    horizontalPitchRangeSlider->setRoundness(7.0f);

    pitchPage->addAndMakeVisible(*horizontalPitchRangeSlider);
    horizontalPitchRangeSlider->setBounds(40, 100, 730, 25);

    horizontalPitchRangeAttachment = std::make_unique<HorizontalRangeSliderAttachment>(
        processorRef.parameters,
        "pitchRangeLower",
        "pitchRangeUpper",
        *horizontalPitchRangeSlider
    );

    horizontalPitchRangeTooltipOverlay = std::make_unique<TooltipOverlay>(*horizontalPitchRangeSlider);
    pitchPage->addAndMakeVisible(*horizontalPitchRangeTooltipOverlay);
    horizontalPitchRangeTooltipOverlay->setBounds(pitchPage->getLocalBounds());
    horizontalPitchRangeTooltipOverlay->toFront(false);

    // Pitch mode (sequence)
    pitchModeDropdown = std::make_unique<ThemedDropdown>();
    pitchPage->addAndMakeVisible(*pitchModeDropdown);
    pitchModeDropdown->setBounds(500, 0, 180, 32);

    pitchModeAttachment = std::make_unique<ThemedDropdown::Attachment>(
        processorRef.parameters,
        "pitchMode",
        *pitchModeDropdown
    );

    uiHelpers.CreateLabel(*pitchPage, pitchModeLabel,
        "Sequence:", 12.0f, 0, 0);

    pitchModeLabel->setBounds(340, 0, 220, 32);

    uiHelpers.CreateKnobExt(*pitchPage, pitchWetMixKnob, pitchWetMixAttachment, "pitchWetMix",
        "", 70, 200, 750, 50);

    pitchWetMixKnob->setTextBoxStyle(juce::Slider::TextBoxRight, false,
        pitchWetMixKnob->getTextBoxWidth(), pitchWetMixKnob->getTextBoxHeight());

    uiHelpers.CreateKnobLabel(*pitchPage, pitchWetMixLabel,
        *pitchWetMixKnob, "  Mix   ", 12.0f, 45.0f);

    //uiHelpers.CreateLabel(*pitchPage, pitchWetMixLabel, "  Mix  ", 12.0f, 50, 20);
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
    graphics.setColour(juce::Colours::red);
    graphics.drawRect(getLocalBounds(), 2);

    // Draw bounding boxes for tabbed page box children
    if (bottomTabbedPageBox != nullptr)
    {
        // Box around the tabbed page box itself
        graphics.setColour(juce::Colours::cyan);
        graphics.drawRect(bottomTabbedPageBox->getBounds(), 2);

        // Boxes around each page component's children
        for (int ChildIndex = 0; ChildIndex < bottomTabbedPageBox->getNumChildComponents(); ++ChildIndex)
        {
            auto* Child = bottomTabbedPageBox->getChildComponent(ChildIndex);

            if (Child != nullptr && Child->isVisible())
            {
                // Draw box around the page itself (in editor coords)
                juce::Rectangle PageBounds = Child->getBounds()
                    .translated(bottomTabbedPageBox->getX(), bottomTabbedPageBox->getY());

                graphics.setColour(juce::Colours::orange);
                graphics.drawRect(PageBounds, 2);

                // Draw boxes around each child inside the page
                for (int PageChildIndex = 0; PageChildIndex < Child->getNumChildComponents(); ++PageChildIndex)
                {
                    auto* PageChild = Child->getChildComponent(PageChildIndex);

                    if (PageChild != nullptr)
                    {
                        juce::Rectangle PageChildBounds = PageChild->getBounds()
                            .translated(PageBounds.getX(), PageBounds.getY());

                        graphics.setColour(juce::Colours::magenta);
                        graphics.drawRect(PageChildBounds, 1);
                    }
                }
            }
        }
    }

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

    if (pitchPage != nullptr && horizontalPitchRangeTooltipOverlay != nullptr)
    {
        horizontalPitchRangeTooltipOverlay->setBounds(pitchPage->getLocalBounds());
        horizontalPitchRangeTooltipOverlay->toFront(false);
    }
}

// Forward keyboard input to the Keyboard Synth in the processor.
bool AudioPluginAudioProcessorEditor::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    juce::ignoreUnused(originatingComponent);

    // Impulse click
    if (key.getKeyCode() == juce::KeyPress::tabKey)
    {
        processorRef.ImpulseClick.Trigger();
        return true;
    }

    // Keyboard synth
    int KeyCode = 0;

    // Prefer text character when available (letters), fallback to key code.
    if (key.getTextCharacter() != 0)
        KeyCode = static_cast<int>(key.getTextCharacter());
    else
        KeyCode = key.getKeyCode();

    processorRef.KeyboardSynth.HandleKeyChange(KeyCode, true);
    lastHeldKeyCodes.insert(KeyCode);

    return true;
}

bool AudioPluginAudioProcessorEditor::keyStateChanged(bool isKeyDown, juce::Component* originatingComponent)
{
    juce::ignoreUnused(isKeyDown, originatingComponent);

    // Poll the mapped key set and compare against lastHeldKeyCodes to detect releases/presses.
    std::unordered_set<int> currentHeld;

    for (int KeyCode : processorRef.KeyboardSynth.GetMappedKeyCodes())
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
            processorRef.KeyboardSynth.HandleKeyChange(KeyCode, true);
    }

    // Released
    for (int KeyCode : lastHeldKeyCodes)
    {
        if (currentHeld.find(KeyCode) == currentHeld.end())
            processorRef.KeyboardSynth.HandleKeyChange(KeyCode, false);
    }

    lastHeldKeyCodes.swap(currentHeld);
    return true; // Consume
}

void AudioPluginAudioProcessorEditor::updateDelayKnobDisplay(int ModeIndex)
{
    if (delayTimeKnob == nullptr)
        return;

    static constexpr float SnapPositions[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    static const char* BeatNames[5] = { "1/1", "1/2", "1/4", "1/8", "1/16" };

    if (ModeIndex == 0) // ms mode — map 0..1 -> 1..1000 ms
    {
        delayTimeKnob->setValueToTextFunction([](double NormalizedValue) -> juce::String
        {
            const float Ms = 1.0f + static_cast<float>(NormalizedValue) * (1000.0f - 1.0f);
            return juce::String(static_cast<int>(std::round(Ms))) + " ms";
        });
    }
    else
    {
        const bool IsTriplet = (ModeIndex == 2);
        const bool IsDotted  = (ModeIndex == 3);

        delayTimeKnob->setValueToTextFunction([IsTriplet, IsDotted](double NormalizedValue) -> juce::String
        {
            int StepIndex = 0;
            float SmallestDistance = std::abs(static_cast<float>(NormalizedValue) - SnapPositions[0]);

            for (int i = 1; i < 5; ++i)
            {
                const float Distance = std::abs(static_cast<float>(NormalizedValue) - SnapPositions[i]);

                if (Distance < SmallestDistance)
                {
                    SmallestDistance = Distance;
                    StepIndex = i;
                }
            }

            juce::String NoteName(BeatNames[StepIndex]);

            if (IsTriplet) return NoteName + "t";
            if (IsDotted)  return NoteName + ".";
            return NoteName;
        });

        delayTimeKnob->setTextValueSuffix("");
    }

    delayTimeKnob->updateText();
}

void AudioPluginAudioProcessorEditor::snapDelayKnobToNearestStep()
{
    if (delayTimeKnob == nullptr)
        return;

    auto* ModeParameter = processorRef.parameters.getParameter("delayTimeMode");

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
