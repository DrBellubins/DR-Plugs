#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

// TODO: Tail is a circle, needs to be rounded rectangle
// TODO: Animation is too slow

// SegmentedButton
// - A header-only, rounded segmented control with an arbitrary number of options.
// - End segments have rounded corners; inner segments have square edges.
// - Selected segment uses ThemePink; unselected segments use AccentGray.
// - Supports parameter attachments via nested Attachment helpers:
//     * ChoiceAttachment -> attach to an AudioProcessorValueTreeState choice parameter.
//     * ExclusiveBooleansAttachment -> attach each segment to a boolean parameter (exclusive/radio behavior).
//
// Usage example (Choice):
//     auto* Seg = new SegmentedButton({"ms", "1/4", "1/8", "1/8T", "1/8."});
//     choiceAttachment = std::make_unique<SegmentedButton::ChoiceAttachment>(apvts, "delayMode", *Seg);
//
// Usage example (Exclusive booleans):
//     auto* Seg = new SegmentedButton({"A", "B", "C"});
//     exclusiveAttachment = std::make_unique<SegmentedButton::ExclusiveBooleansAttachment>(apvts,
//         std::vector<juce::String>{ "modeA", "modeB", "modeC" }, *Seg);
class SegmentedButton : public juce::Component
{
public:
    // ============================ Construction ============================

    SegmentedButton()
    {
        setInterceptsMouseClicks(true, true);
        labelFont = juce::Font("Liberation Sans", 14.0f, juce::Font::bold);
    }

    explicit SegmentedButton(const juce::StringArray& OptionLabels)
    {
        setInterceptsMouseClicks(true, true);
        labelFont = juce::Font("Liberation Sans", 14.0f, juce::Font::bold);
        setOptions(OptionLabels);
    }

    ~SegmentedButton() override = default;

    // ============================ Options API ============================

    void setOptions(const juce::StringArray& OptionLabels)
    {
        options = OptionLabels;

        if (options.isEmpty())
            selectedIndex = -1;
        else
        {
            if (selectedIndex < 0 || selectedIndex >= options.size())
                selectedIndex = 0;
        }

        repaint();
    }

    const juce::StringArray& getOptions() const
    {
        return options;
    }

    int getNumOptions() const
    {
        return options.size();
    }

    // ============================ Selection API ============================

    // Sets the selected index and optionally notifies listeners.
    void setSelectedIndex(int NewSelectedIndex, juce::NotificationType Notification)
    {
        const int ClampedIndex = juce::jlimit(-1, options.size() - 1, NewSelectedIndex);

        if (selectedIndex == ClampedIndex)
            return;

        selectedIndex = ClampedIndex;
        repaint();

        if (Notification == juce::sendNotificationAsync || Notification == juce::sendNotification)
        {
            if (onSelectionChanged != nullptr)
                onSelectionChanged(selectedIndex);
        }
    }

    // Sets the selected index without triggering the onSelectionChanged callback.
    void setSelectedIndexSilently(int NewSelectedIndex)
    {
        const int ClampedIndex = juce::jlimit(-1, options.size() - 1, NewSelectedIndex);

        if (selectedIndex == ClampedIndex)
            return;

        const bool WasBlocked = blockSelectionCallback;
        blockSelectionCallback = true;

        selectedIndex = ClampedIndex;
        repaint();

        blockSelectionCallback = WasBlocked;
    }

    int getSelectedIndex() const
    {
        return selectedIndex;
    }

    juce::String getSelectedText() const
    {
        if (selectedIndex >= 0 && selectedIndex < options.size())
            return options[selectedIndex];

        return {};
    }

    // Callback invoked when selection changes via setSelectedIndex (notify) or user interaction.
    std::function<void(int)> onSelectionChanged;

    // ============================ Appearance API ============================

    void setCornerRadius(float NewCornerRadius)
    {
        cornerRadius = std::max(0.0f, NewCornerRadius);
        repaint();
    }

    void setDividerThickness(float NewDividerThickness)
    {
        dividerThickness = std::max(0.0f, NewDividerThickness);
        repaint();
    }

    void setFont(const juce::Font& NewFont)
    {
        labelFont = NewFont;
        repaint();
    }

    // ============================ JUCE Overrides ============================

    void paint(juce::Graphics& GraphicsContext) override
    {
        const juce::Rectangle<int> Bounds = getLocalBounds();

        // Background outline (optional subtle border)
        //GraphicsContext.setColour(UnfocusedGray);
        //GraphicsContext.fillRoundedRectangle(Bounds.toFloat(), cornerRadius);

        if (options.isEmpty())
            return;

        // Compute segment rectangles
        const int NumberOfOptions = options.size();
        const float TotalWidth = static_cast<float>(Bounds.getWidth());
        const float TotalHeight = static_cast<float>(Bounds.getHeight());
        const float SegmentWidthFloat = TotalWidth / static_cast<float>(NumberOfOptions);

        // Draw each segment
        for (int OptionIndex = 0; OptionIndex < NumberOfOptions; ++OptionIndex)
        {
            const float X = static_cast<float>(Bounds.getX()) + std::floor(SegmentWidthFloat * static_cast<float>(OptionIndex));
            const float W = (OptionIndex == NumberOfOptions - 1)
                ? (static_cast<float>(Bounds.getRight()) - X) // last takes remainder
                : std::floor(SegmentWidthFloat);

            const juce::Rectangle<float> SegmentBounds(X, static_cast<float>(Bounds.getY()), W, TotalHeight);

            const bool IsFirst = (OptionIndex == 0);
            const bool IsLast = (OptionIndex == NumberOfOptions - 1);
            const bool IsSelected = (OptionIndex == selectedIndex);

            // Fill color
            const juce::Colour FillColour = IsSelected ? ThemePink : AccentGray;

            // Build path with appropriate corner rounding
            juce::Path SegmentPath;

            SegmentPath.addRoundedRectangle(SegmentBounds.getX(), SegmentBounds.getY(),
                SegmentBounds.getWidth(), SegmentBounds.getHeight(), cornerRadius,
                cornerRadius, IsFirst, IsLast, IsFirst, IsLast);

            // Fill
            GraphicsContext.setColour(FillColour);
            GraphicsContext.fillPath(SegmentPath);

            // Hover / focus highlight (optional subtle)
            if (hoveredIndex == OptionIndex && isEnabled())
            {
                GraphicsContext.setColour(FocusedGray.withMultipliedAlpha(0.10f));
                GraphicsContext.fillPath(SegmentPath);
            }

            // Divider between segments (skip after last)
            if (!IsLast && dividerThickness > 0.0f)
            {
                GraphicsContext.setColour(BGGray.darker(0.2f));
                const float DividerX = SegmentBounds.getRight();
                GraphicsContext.fillRect(juce::Rectangle<float>(DividerX - (dividerThickness * 0.5f),
                                                                SegmentBounds.getY() + 2.0f,
                                                                dividerThickness,
                                                                SegmentBounds.getHeight() - 4.0f));
            }

            // Text
            GraphicsContext.setColour(juce::Colours::white);
            GraphicsContext.setFont(labelFont);

            const auto TextBounds = SegmentBounds.reduced(6.0f, 4.0f).toNearestInt();
            GraphicsContext.drawFittedText(options[OptionIndex],
                                           TextBounds,
                                           juce::Justification::centred,
                                           1);
        }

        // Outline on top for crisp edge
        GraphicsContext.setColour(UnfocusedGray.brighter(0.1f));
        GraphicsContext.drawRoundedRectangle(Bounds.toFloat().reduced(0.5f), cornerRadius, 1.0f);
    }

    void mouseMove(const juce::MouseEvent& MouseEvent) override
    {
        juce::ignoreUnused(MouseEvent);
        const int NewHoveredIndex = getIndexFromX(static_cast<float>(MouseEvent.x));

        if (hoveredIndex != NewHoveredIndex)
        {
            hoveredIndex = NewHoveredIndex;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& MouseEvent) override
    {
        juce::ignoreUnused(MouseEvent);
        hoveredIndex = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& MouseEvent) override
    {
        if (!isEnabled() || options.isEmpty())
            return;

        const int ClickedIndex = getIndexFromX(static_cast<float>(MouseEvent.x));

        if (ClickedIndex >= 0 && ClickedIndex < options.size())
        {
            // Begin gesture callback (for attachments)
            if (onGestureBegin != nullptr)
                onGestureBegin();

            if (onGestureCommit != nullptr)
                onGestureCommit(ClickedIndex);

            // Update selection with notification
            setSelectedIndex(ClickedIndex, juce::sendNotificationAsync);
        }
    }

    void mouseUp(const juce::MouseEvent& MouseEvent) override
    {
        juce::ignoreUnused(MouseEvent);

        if (!isEnabled())
            return;

        // End gesture callback (for attachments)
        if (onGestureEnd != nullptr)
            onGestureEnd();
    }

    // ============================ Attachments ============================
    // These helper classes allow the control to be bound to parameters.

    // ChoiceAttachment: binds the SegmentedButton to a single AudioParameterChoice (or other discrete parameter).
    // It updates the control when the parameter changes, and writes parameter changes when the user clicks a segment.
    class ChoiceAttachment  : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        ChoiceAttachment(juce::AudioProcessorValueTreeState& State,
                         const juce::String& ParameterID,
                         SegmentedButton& SegmentedControl)
            : apvts(State),
              parameterID(ParameterID),
              control(SegmentedControl)
        {
            parameter = apvts.getParameter(parameterID);

            jassert(parameter != nullptr && "ChoiceAttachment: Parameter ID not found!");

            // If this is a choice parameter and the control has no options, adopt the parameter's choice labels.
            if (auto* ChoiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter))
            {
                if (control.getNumOptions() == 0)
                    control.setOptions(ChoiceParameter->choices);
            }

            // Wire control -> parameter
            control.onGestureBegin = [this]()
            {
                if (parameter != nullptr)
                    parameter->beginChangeGesture();
            };

            control.onGestureCommit = [this](int NewIndex)
            {
                if (parameter != nullptr)
                {
                    const float RawTargetValue = static_cast<float>(NewIndex);
                    const float Normalised = parameter->convertTo0to1(RawTargetValue);
                    parameter->setValueNotifyingHost(Normalised);
                }
            };

            control.onGestureEnd = [this]()
            {
                if (parameter != nullptr)
                    parameter->endChangeGesture();
            };

            // Parameter -> control
            apvts.addParameterListener(parameterID, this);

            // Initial sync from parameter
            if (parameter != nullptr)
            {
                const float RawValue = parameter->convertFrom0to1(parameter->getValue());
                control.setSelectedIndexSilently(static_cast<int>(std::round(RawValue)));
            }
        }

        ~ChoiceAttachment() override
        {
            apvts.removeParameterListener(parameterID, this);

            // Detach gesture lambdas to avoid dangling references
            control.onGestureBegin = nullptr;
            control.onGestureCommit = nullptr;
            control.onGestureEnd = nullptr;
        }

        void parameterChanged(const juce::String& ChangedParameterID, float NewValue) override
        {
            if (ChangedParameterID != parameterID)
                return;

            const int NewIndex = static_cast<int>(std::round(NewValue));

            juce::MessageManager::callAsync([this, NewIndex]()
            {
                control.setSelectedIndexSilently(NewIndex);
                control.repaint();
            });
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::String parameterID;
        juce::RangedAudioParameter* parameter = nullptr;
        SegmentedButton& control;
    };

    // ExclusiveBooleansAttachment: binds each segment to a boolean parameter, enforces radio exclusivity.
    // The number of parameter IDs must equal the number of options in the control.
    class ExclusiveBooleansAttachment  : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        ExclusiveBooleansAttachment(juce::AudioProcessorValueTreeState& State,
                                    const std::vector<juce::String>& ParameterIDs,
                                    SegmentedButton& SegmentedControl)
            : apvts(State),
              parameterIDs(ParameterIDs),
              control(SegmentedControl)
        {
            jassert(control.getNumOptions() == static_cast<int>(parameterIDs.size())
                    && "ExclusiveBooleansAttachment: Option count must match parameter IDs count.");

            // Collect parameters and validate types
            for (const auto& Id : parameterIDs)
            {
                auto* Parameter = apvts.getParameter(Id);
                jassert(Parameter != nullptr && "ExclusiveBooleansAttachment: Parameter ID not found!");

                auto* BoolParameter = dynamic_cast<juce::AudioParameterBool*>(Parameter);
                jassert(BoolParameter != nullptr && "ExclusiveBooleansAttachment: All parameters must be AudioParameterBool.");

                parameters.push_back(BoolParameter);
                apvts.addParameterListener(Id, this);
            }

            // Control -> parameters
            control.onGestureBegin = [this]()
            {
                for (auto* P : parameters)
                {
                    if (P != nullptr)
                        P->beginChangeGesture();
                }
            };

            control.onGestureCommit = [this](int NewIndex)
            {
                if (ignoreParameterCallbacks)
                    return;

                ignoreParameterCallbacks = true;

                for (int ParameterIndex = 0; ParameterIndex < static_cast<int>(parameters.size()); ++ParameterIndex)
                {
                    auto* P = parameters[static_cast<size_t>(ParameterIndex)];

                    if (P != nullptr)
                    {
                        const bool ShouldBeOn = (ParameterIndex == NewIndex);
                        P->setValueNotifyingHost(ShouldBeOn ? 1.0f : 0.0f);
                    }
                }

                ignoreParameterCallbacks = false;
            };

            control.onGestureEnd = [this]()
            {
                for (auto* P : parameters)
                {
                    if (P != nullptr)
                        P->endChangeGesture();
                }
            };

            // Initial sync: if any parameter is true, select it; otherwise, select the first and set it true.
            int FoundIndex = -1;

            for (int ParameterIndex = 0; ParameterIndex < static_cast<int>(parameters.size()); ++ParameterIndex)
            {
                if (parameters[static_cast<size_t>(ParameterIndex)]->get())
                {
                    FoundIndex = ParameterIndex;
                    break;
                }
            }

            if (FoundIndex < 0 && !parameters.empty())
            {
                ignoreParameterCallbacks = true;

                parameters[0]->beginChangeGesture();
                parameters[0]->setValueNotifyingHost(1.0f);

                for (size_t ParameterIndex = 1; ParameterIndex < parameters.size(); ++ParameterIndex)
                    parameters[ParameterIndex]->setValueNotifyingHost(0.0f);

                parameters[0]->endChangeGesture();

                ignoreParameterCallbacks = false;
                FoundIndex = 0;
            }

            control.setSelectedIndexSilently(FoundIndex);
        }

        ~ExclusiveBooleansAttachment() override
        {
            for (const auto& Id : parameterIDs)
                apvts.removeParameterListener(Id, this);

            control.onGestureBegin = nullptr;
            control.onGestureCommit = nullptr;
            control.onGestureEnd = nullptr;
        }

        void parameterChanged(const juce::String& ChangedParameterID, float NewValue) override
        {
            if (ignoreParameterCallbacks)
                return;

            // Determine which boolean is currently set, preferring the one that just changed to 1
            int Selected = -1;

            if (NewValue >= 0.5f)
            {
                for (size_t ParameterIndex = 0; ParameterIndex < parameterIDs.size(); ++ParameterIndex)
                {
                    if (parameterIDs[ParameterIndex] == ChangedParameterID)
                    {
                        Selected = static_cast<int>(ParameterIndex);
                        break;
                    }
                }
            }
            else
            {
                // If this one turned off, find any other that is on
                for (size_t ParameterIndex = 0; ParameterIndex < parameters.size(); ++ParameterIndex)
                {
                    if (parameters[ParameterIndex]->get())
                    {
                        Selected = static_cast<int>(ParameterIndex);
                        break;
                    }
                }
            }

            juce::MessageManager::callAsync([this, Selected]()
            {
                control.setSelectedIndexSilently(Selected);
                control.repaint();
            });
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        std::vector<juce::String> parameterIDs;
        std::vector<juce::AudioParameterBool*> parameters;
        SegmentedButton& control;
        std::atomic<bool> ignoreParameterCallbacks { false };
    };

    // ============================ Gesture bridge ============================
    // These are used by attachments to wire user gestures to parameter gestures.
    std::function<void()> onGestureBegin;
    std::function<void(int)> onGestureCommit;
    std::function<void()> onGestureEnd;

private:
    // ============================ Helpers ============================

    int getIndexFromX(float XPosition) const
    {
        const int NumberOfOptions = options.size();

        if (NumberOfOptions <= 0)
            return -1;

        const float TotalWidth = static_cast<float>(getWidth());
        const float SegmentWidthFloat = TotalWidth / static_cast<float>(NumberOfOptions);

        int Index = static_cast<int>(std::floor(juce::jlimit(0.0f, TotalWidth - 1.0f, XPosition) / SegmentWidthFloat));
        Index = juce::jlimit(0, NumberOfOptions - 1, Index);

        return Index;
    }

    // ============================ State ============================

    juce::StringArray options;
    int selectedIndex = -1;
    int hoveredIndex = -1;

    float cornerRadius = 10.0f;
    float dividerThickness = 1.0f;
    juce::Font labelFont;

    bool blockSelectionCallback = false;
};