#pragma once

#include <utility>
#include "BinaryData.h"
#include "PluginProcessor.h"

#include "Utils/FlatLabel.h"
#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/RoundedToggle.h"
#include "Utils/SegmentedButton.h"
#include "Utils/Theme.h"
#include "Utils/ThemedKnob.h"
#include "Utils/ThemedSlider.h"

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor, public juce::KeyListener
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void createPrePostToggle(juce::AudioProcessorValueTreeState& state,
        std::unique_ptr<RoundedToggle>& toggle,
        std::unique_ptr<RoundedToggle::Attachment>& attachment,
        RoundedToggle::Orientation orientation,
        const juce::String& parameterID,
        int width, int height, int offsetFromCenterX, int offsetFromCenterY);

    void createSlider(std::unique_ptr<ThemedSlider>& slider, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment, juce::String paramID,
        int width, int height, int offsetFromCenterX, int offsetFromCenterY);

    void createSliderLabel(std::unique_ptr<juce::Label>& label, ThemedSlider& slider,
        juce::String text, float fontSize, int offsetX);

    void createKnob(std::unique_ptr<ThemedKnob>& knob, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment, juce::String paramID,
        juce::String suffix, int widthHeight, int offsetFromCenterX, int offsetFromCenterY);

    void createKnobLabel(std::unique_ptr<juce::Label>& label, ThemedKnob& knob,
        juce::String text, float fontSize, int offsetY);

    void centerKnobLabel(std::unique_ptr<juce::Label>& label, ThemedKnob& knob, int offsetY);

    int getLabelWidth(std::unique_ptr<juce::Label>& label);

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    // KeyListener overrides to forward computer keyboard input to the synth.
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* originatingComponent) override;

private:
    FlatRotaryLookAndFeel flatKnobLAF;

    // Normalized discrete positions for beat subdivisions (count = 9).
    // These correspond to indices used by the DSP fraction table.
    static constexpr float DelaySyncNormalizedPositions[9] =
    {
        0.0f,
        0.125f,
        0.25f,
        0.375f,
        0.5f,
        0.625f,
        0.75f,
        0.875f,
        1.0f
    };

    void snapDelayKnobToNearestStep();

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor& processorRef;

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

    // Pre-Post toggles
    std::unique_ptr<RoundedToggle> hplpFilterToggle;
    std::unique_ptr<RoundedToggle::Attachment> hplpFilterToggleAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
