#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"
#include "ThemeContext.h"

class ThemedDropdown : public juce::Component
{
public:
    ThemedDropdown();
    ~ThemedDropdown() override;

    void paint(juce::Graphics& GraphicsContext) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& MouseEvent) override;

    void SetJustification(juce::Justification justificationType);
    void SetCornerRadius(float newCornerRadius);
    void SetOutlineThickness(float newOutlineThickness);
    void SetItemHeight(int newItemHeight);

    void addItem(const juce::String& itemText, int itemID);
    void clear(juce::NotificationType notificationType = juce::dontSendNotification);

    int getNumItems() const;
    int getSelectedItemIndex() const;
    void setSelectedItemIndex(int newSelectedItemIndex, juce::NotificationType notificationType);
    juce::String getText() const;

    std::function<void()> onChange;

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
    struct Item
    {
        juce::String text;
        int itemID = 0;
    };

    class PopupList : public juce::Component
    {
    public:
        explicit PopupList(ThemedDropdown& ownerDropdown);

        void paint(juce::Graphics& GraphicsContext) override;
        void mouseMove(const juce::MouseEvent& MouseEvent) override;
        void mouseExit(const juce::MouseEvent& MouseEvent) override;
        void mouseDown(const juce::MouseEvent& MouseEvent) override;

        void UpdateBounds();
        int GetItemIndexAtPosition(juce::Point<int> mousePosition) const;
        juce::Rectangle<int> GetItemBounds(int itemIndex) const;

    private:
        ThemedDropdown& owner;
        int hoveredItemIndex = -1;
    };

    juce::Rectangle<int> GetClosedBounds() const;
    juce::Rectangle<int> GetArrowBounds() const;
    void SetExpanded(bool shouldBeExpanded);

    juce::Array<Item> items;
    int selectedItemIndex = -1;
    bool isExpanded = false;

    juce::Justification textJustification = juce::Justification::centredLeft;
    float cornerRadius = 8.0f;
    float outlineThickness = 1.0f;
    int itemHeight = 30;
    int closedHeight = 32;

    PopupList popupList;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedDropdown)
};