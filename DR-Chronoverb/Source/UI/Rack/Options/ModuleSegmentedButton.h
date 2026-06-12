#pragma once

#include "ModuleOption.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

class ModuleSegmentedButton : public ModuleOption
{
public:
    ModuleSegmentedButton();
    ~ModuleSegmentedButton() override = default;

    void SetOptions(const juce::StringArray& newOptions);
    const juce::StringArray& GetOptions() const;

    void SetSelectedIndex(int newIndex,
                          juce::NotificationType notification = juce::dontSendNotification);

    void SetSelectedIndexSilently(int newIndex);
    int GetSelectedIndex() const;
    juce::String GetSelectedText() const;

    void AttachToParameter(juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& parameterID);

    void SetCornerRadius(float newCornerRadius);
    void SetDividerThickness(float newDividerThickness);
    void SetFont(const juce::Font& newFont);

    void SetControlBounds(const juce::Rectangle<int>& newBounds);
    juce::Rectangle<int> GetControlBounds() const;

    void SetLabelHeight(int newLabelHeight);
    int GetLabelHeight() const;

    void ApplyTheme(const RackTheme& rackTheme) override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;

    std::function<void(int)> onSelectionChanged;

private:
    class ChoiceAttachment : private juce::AudioProcessorValueTreeState::Listener
    {
    public:
        ChoiceAttachment(juce::AudioProcessorValueTreeState& state,
                         const juce::String& parameterID,
                         ModuleSegmentedButton& controlToAttach);

        ~ChoiceAttachment() override;

    private:
        void parameterChanged(const juce::String& changedParameterID, float newValue) override;

        juce::AudioProcessorValueTreeState& apvts;
        juce::String attachedParameterID;
        juce::RangedAudioParameter* parameter = nullptr;
        ModuleSegmentedButton& control;
    };

    int GetIndexFromPosition(float x) const;
    juce::Rectangle<int> GetResolvedControlBounds() const;

    juce::StringArray options;
    int selectedIndex = -1;
    int hoveredIndex = -1;

    juce::Font buttonFont;
    float cornerRadius = 8.0f;
    float dividerThickness = 1.0f;

    juce::Rectangle<int> controlBoundsOverride;
    int labelHeight = 16;

    std::unique_ptr<ChoiceAttachment> attachment;

    std::function<void()> onGestureBegin;
    std::function<void(int)> onGestureCommit;
    std::function<void()> onGestureEnd;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleSegmentedButton)
};