#pragma once

#include <utility>
#include "BinaryData.h"
#include "PluginProcessor.h"

#include "Utils/UIHelpers.h"
#include "Utils/SegmentedButton.h"
#include "Utils/Theme.h"
#include "Utils/TabbedPageBox.h"

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::KeyListener
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    // KeyListener overrides to forward computer keyboard input to the synth.
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;

private:
    const int nonPitchYOffset = 20;
    const int pitchYOffset = 230;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

    FlatRotaryLookAndFeel flatKnobLAF;
    UIHelpers uiHelpers;

    // Beat subdivision knob snap points (5 entries: Whole, Half, Quarter, Eighth, Sixteenth)
    // Linear mapping: index / (Count - 1) -> {0.0, 0.25, 0.5, 0.75, 1.0}
    static constexpr float DelaySyncNormalizedPositions[5] =
    {
        0.0f,
        1.0f / 4.0f,
        2.0f / 4.0f,
        3.0f / 4.0f,
        1.0f
    };

    void snapDelayKnobToNearestStep();

    juce::Image background;
    juce::Image logo;

    // Delay knobs
    std::unique_ptr<ThemedKnob> delayTimeKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> delayTimeAttachment;

    std::unique_ptr<ThemedKnob> feedbackTimeKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feedbackTimeAttachment;

    std::unique_ptr<ThemedKnob> diffusionAmountKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionAmountAttachment;

    std::unique_ptr<ThemedKnob> diffusionSizeKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionSizeAttachment;

    std::unique_ptr<ThemedSlider> diffusionQualitySlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> diffusionQualityAttachment;

    std::unique_ptr<ThemedKnob> dryWetMixKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dryWetMixAttachment;

    // Filter knobs
    std::unique_ptr<ThemedKnob> stereoSpreadKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> stereoSpreadAttachment;

    std::unique_ptr<ThemedKnob> lowPassKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowPassAttachment;

    std::unique_ptr<ThemedKnob> highPassKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> highPassAttachment;

    // Ducking knobs
    std::unique_ptr<ThemedKnob> duckAmountKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckAmountAttachment;

    std::unique_ptr<ThemedKnob> duckAttackKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckAttackAttachment;

    std::unique_ptr<ThemedKnob> duckReleaseKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> duckReleaseAttachment;

    // Delay buttons
    std::unique_ptr<SegmentedButton> delayTimeModeButtons;
    std::unique_ptr<SegmentedButton::ChoiceAttachment> delayTimeModeAttachment;

    // Delay labels
    std::unique_ptr<juce::Label> delayTimeLabel;
    std::unique_ptr<juce::Label> feedbackLabel;
    std::unique_ptr<juce::Label> diffusionAmountLabel;
    std::unique_ptr<juce::Label> diffusionSizeLabel;
    std::unique_ptr<juce::Label> diffusionQualityLabel;
    std::unique_ptr<juce::Label> dryWetMixLabel;

    // Filter labels
    std::unique_ptr<juce::Label> stereoSpreadLabel;
    std::unique_ptr<juce::Label> lowPassLabel;
    std::unique_ptr<juce::Label> highPassLabel;

    // Ducking labels
    std::unique_ptr<juce::Label> duckAmountLabel;
    std::unique_ptr<juce::Label> duckAttackLabel;
    std::unique_ptr<juce::Label> duckReleaseLabel;

    std::unordered_set<int> lastHeldKeyCodes;

    // Pre-Post toggle
    std::unique_ptr<RoundedToggle> hplpFilterToggle;
    std::unique_ptr<RoundedToggle::Attachment> hplpFilterToggleAttachment;

    // Tabbed page box
    std::unique_ptr<TabbedPageBox> bottomTabbedPageBox;

    std::unique_ptr<juce::Component> pitchPage;
    std::unique_ptr<juce::Component> distortionPage;
    std::unique_ptr<juce::Component> tapePage;
    std::unique_ptr<juce::Component> granularPage;

    // Pitch shift toggle
    std::unique_ptr<ThemedCheckbox> pitchShiftToggle;
    std::unique_ptr<ThemedCheckbox::Attachment> pitchShiftToggleAttachment;
    std::unique_ptr<juce::Label> pitchShiftTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
