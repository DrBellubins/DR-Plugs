#pragma once

#include "../PluginEditor.h"

class ThemedCheckbox;

class PitchPageLayout
{
public:
    void CreatePitchPageLayout(juce::Component& parentPage, UIHelpers uiHelpers,
        AudioPluginAudioProcessor& processorRef)
    {
        uiHelpers.CreateCheckbox(parentPage, pitchShiftStereoToggle,
            pitchShiftStereoToggleAttachment,
            "pitchStereoEnabled",
            20, 20, 300, 35);

        uiHelpers.CreateCheckboxLabel(parentPage, pitchShiftStereoLabel, *pitchShiftStereoToggle,
            "Stereo", 14.0f, -38);

        // Horizontal slider
        horizontalPitchRangeSlider = std::make_unique<HorizontalRangeSlider>(-48.0f, 48.0f);
        horizontalPitchRangeSlider->setMinimumRange(0.0f);
        horizontalPitchRangeSlider->setSteppingEnabled(true);
        horizontalPitchRangeSlider->setStepSize(12.0f);
        horizontalPitchRangeSlider->setRoundness(7.0f);

        parentPage.addAndMakeVisible(*horizontalPitchRangeSlider);
        horizontalPitchRangeSlider->setBounds(40, 100, 730, 25);

        horizontalPitchRangeAttachment = std::make_unique<HorizontalRangeSliderAttachment>(
            processorRef.parameters,
            "pitchRangeLower",
            "pitchRangeUpper",
            *horizontalPitchRangeSlider
        );

        horizontalPitchRangeTooltipOverlay = std::make_unique<TooltipOverlay>(*horizontalPitchRangeSlider);
        parentPage.addAndMakeVisible(*horizontalPitchRangeTooltipOverlay);
        horizontalPitchRangeTooltipOverlay->setBounds(parentPage.getLocalBounds());
        horizontalPitchRangeTooltipOverlay->toFront(false);

        // Pitch mode (sequence)
        constexpr int sequenceWidth = 180;
        pitchSequenceDropdown = std::make_unique<ThemedDropdown>();
        parentPage.addAndMakeVisible(*pitchSequenceDropdown);
        pitchSequenceDropdown->setBounds(30, 45, sequenceWidth, 32);

        pitchSequenceAttachment = std::make_unique<ThemedDropdown::Attachment>(
            processorRef.parameters,
            "pitchSequence",
            *pitchSequenceDropdown
        );

        uiHelpers.CreateLabel(parentPage, pitchSequenceLabel,
            "Sequence", 14.0f, 30 + (sequenceWidth / 2), 22);

        constexpr int pWetWidth = 130;

        uiHelpers.CreateKnobExt(parentPage, pitchWetMixKnob, pitchWetMixAttachment, "pitchWetMix",
            "", pWetWidth, 50, 750, 50);

        pitchWetMixKnob->setTextBoxStyle(juce::Slider::TextBoxRight, false,
            pitchWetMixKnob->getTextBoxWidth(), pitchWetMixKnob->getTextBoxHeight());

        uiHelpers.CreateLabel(parentPage, pitchWetMixLabel, "  Mix  ", 12.0f, 0, 0);

        const int pWetLabelWidth = pitchWetMixLabel->getWidth();
        const int pWetLabelHeight = pitchWetMixLabel->getHeight();

        pitchWetMixLabel->setBounds((750 - (pWetWidth / 2)) + (pWetLabelWidth / 2), 0, pWetLabelWidth, pWetLabelHeight);
    }

    std::unique_ptr<ThemedCheckbox> pitchShiftStereoToggle;
    std::unique_ptr<ThemedCheckbox::Attachment> pitchShiftStereoToggleAttachment;
    std::unique_ptr<juce::Label> pitchShiftStereoLabel;

    std::unique_ptr<HorizontalRangeSlider> horizontalPitchRangeSlider;
    std::unique_ptr<HorizontalRangeSliderAttachment> horizontalPitchRangeAttachment;
    std::unique_ptr<TooltipOverlay> horizontalPitchRangeTooltipOverlay;

    std::unique_ptr<ThemedDropdown> pitchSequenceDropdown;
    std::unique_ptr<ThemedDropdown::Attachment> pitchSequenceAttachment;
    std::unique_ptr<juce::Label> pitchSequenceLabel;

    std::unique_ptr<ThemedDropdown> pitchAlgorithmDropdown;
    std::unique_ptr<ThemedDropdown::Attachment> pitchAlgorithmAttachment;
    std::unique_ptr<juce::Label> pitchAlgorithmLabel;

    std::unique_ptr<ThemedKnob> pitchWetMixKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchWetMixAttachment;
    std::unique_ptr<juce::Label> pitchWetMixLabel;
};
