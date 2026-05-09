#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"
#include "ThemeContext.h"

class ThemedDropdown : public juce::ComboBox
{
public:
    ThemedDropdown();
    ~ThemedDropdown() override;

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

        void parameterChanged(
            const juce::String& changedParameterID,
            float newValue) override;

    private:
        void SyncParameterToDropdown();
        void SyncDropdownToParameter();

        juce::AudioProcessorValueTreeState& apvts;
        juce::String attachedParameterID;
        juce::AudioProcessorParameter* parameter = nullptr;
        juce::AudioParameterChoice* choiceParameter = nullptr;
        ThemedDropdown& dropdown;
        std::atomic<bool> ignoreCallbacks { false };
    };

private:
    class DropdownLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        DropdownLookAndFeel(ThemedDropdown& ownerDropdown);

        void drawComboBox(
            juce::Graphics& GraphicsContext,
            int width,
            int height,
            bool isButtonDown,
            int buttonX,
            int buttonY,
            int buttonW,
            int buttonH,
            juce::ComboBox& comboBox) override;

        juce::Font getComboBoxFont(juce::ComboBox& comboBox) override;

        juce::Label* createComboBoxTextBox(juce::ComboBox& comboBox) override;

        void positionComboBoxText(
            juce::ComboBox& comboBox,
            juce::Label& label) override;

        void drawPopupMenuBackgroundWithOptions(
            juce::Graphics& GraphicsContext,
            int width,
            int height,
            const juce::PopupMenu::Options& options) override;

        void drawPopupMenuItem(
            juce::Graphics& GraphicsContext,
            const juce::Rectangle<int>& area,
            bool isSeparator,
            bool isActive,
            bool isHighlighted,
            bool isTicked,
            bool hasSubMenu,
            const juce::String& text,
            const juce::String& shortcutKeyText,
            const juce::Drawable* icon,
            const juce::Colour* textColour) override;

    private:
        ThemedDropdown& owner;
    };

    void UpdateColours();

    juce::Justification textJustification = juce::Justification::centredLeft;
    float cornerRadius = 8.0f;
    float outlineThickness = 1.0f;

    DropdownLookAndFeel dropdownLookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedDropdown)
};