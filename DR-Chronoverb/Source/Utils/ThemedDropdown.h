#pragma once

#include "Theme.h"
#include "ThemeContext.h"
#include <juce_audio_processors/juce_audio_processors.h>

class ThemedDropdown : public juce::ComboBox
{
public:
    ThemedDropdown();
    ~ThemedDropdown() override;

    void paint(juce::Graphics& GraphicsContext) override;
    void resized() override;

    void SetJustification(juce::Justification justificationType);
    void SetCornerRadius(float newCornerRadius);
    void SetOutlineThickness(float newOutlineThickness);

    class Attachment : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        Attachment(
            juce::AudioProcessorValueTreeState& state,
            const juce::String& parameterID,
            ThemedDropdown& dropdownToControl
        );

        ~Attachment() override;

        void parameterChanged(const juce::String& changedParameterID, float newValue) override;

    private:
        void SyncParameterToDropdown();
        void SyncDropdownToParameter();

        juce::AudioProcessorValueTreeState& apvts;
        juce::String attachedParameterID;
        juce::RangedAudioParameter* parameter = nullptr;
        ThemedDropdown& dropdown;
        std::atomic<bool> ignoreCallbacks { false };
    };

private:
    juce::Justification textJustification = juce::Justification::centredLeft;
    float cornerRadius = 8.0f;
    float outlineThickness = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedDropdown)
};
