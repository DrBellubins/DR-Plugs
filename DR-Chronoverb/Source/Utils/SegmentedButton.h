#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <utility>
#include <vector>

#include "Theme.h"
#include "ThemeContext.h"

// SegmentedButton
// - A header-only, rounded segmented control with an arbitrary number of options.
// - Supports horizontal (default) and vertical orientation.
// - End segments have rounded corners; inner segments have square edges.
// - Selected segment uses ThemePink; unselected segments use AccentGray.
// - Supports parameter attachments via nested Attachment helpers:
//     * ChoiceAttachment -> attach to an AudioProcessorValueTreeState choice parameter.
//     * ExclusiveBooleansAttachment -> attach each segment to a boolean parameter (exclusive/radio behavior).
//
// Usage example (Choice):
//     auto* seg = new SegmentedButton({"ms", "1/4", "1/8", "1/8T", "1/8."});
//     choiceAttachment = std::make_unique<SegmentedButton::ChoiceAttachment>(apvts, "delayMode", *seg);
//
// Usage example (Choice, vertical):
//     auto* seg = new SegmentedButton({"ms", "1/4", "1/8"}, true);
//
// Usage example (Exclusive booleans):
//     auto* seg = new SegmentedButton({"A", "B", "C"});
//     exclusiveAttachment = std::make_unique<SegmentedButton::ExclusiveBooleansAttachment>(apvts,
//         std::vector<juce::String>{ "modeA", "modeB", "modeC" }, *seg);
class SegmentedButton : public juce::Component
{
public:
    enum class Orientation
    {
        Horizontal,
        Vertical
    };

    // ============================ Construction ============================

    SegmentedButton()
    {
        initialiseFont();
        setInterceptsMouseClicks(true, true);
    }

    explicit SegmentedButton(const juce::StringArray& optionLabels,
                             bool isVertical = false)
        : orientation(isVertical ? Orientation::Vertical : Orientation::Horizontal)
    {
        initialiseFont();
        setInterceptsMouseClicks(true, true);
        setOptions(optionLabels);
    }

    SegmentedButton(const juce::StringArray& optionLabels,
                    Orientation newOrientation)
        : orientation(newOrientation)
    {
        initialiseFont();
        setInterceptsMouseClicks(true, true);
        setOptions(optionLabels);
    }

    ~SegmentedButton() override = default;

    // ============================ Options API ============================

    void setOptions(const juce::StringArray& optionLabels)
    {
        options = optionLabels;

        if (options.isEmpty())
        {
            selectedIndex = -1;
        }
        else if (selectedIndex < 0 || selectedIndex >= options.size())
        {
            selectedIndex = 0;
        }

        repaint();
    }

    [[nodiscard]] const juce::StringArray& getOptions() const
    {
        return options;
    }

    [[nodiscard]] int getNumOptions() const
    {
        return options.size();
    }

    // ============================ Selection API ============================

    // Sets the selected index and optionally notifies listeners.
    void setSelectedIndex(int newSelectedIndex, juce::NotificationType notification)
    {
        const int clampedIndex = options.isEmpty()
            ? -1
            : juce::jlimit(0, options.size() - 1, newSelectedIndex);

        if (selectedIndex == clampedIndex)
            return;

        selectedIndex = clampedIndex;
        repaint();

        if (!blockSelectionCallback
            && (notification == juce::sendNotificationAsync
                || notification == juce::sendNotification))
        {
            if (onSelectionChanged != nullptr)
                onSelectionChanged(selectedIndex);
        }
    }

    // Sets the selected index without triggering the onSelectionChanged callback.
    void setSelectedIndexSilently(int newSelectedIndex)
    {
        const int clampedIndex = options.isEmpty()
            ? -1
            : juce::jlimit(0, options.size() - 1, newSelectedIndex);

        if (selectedIndex == clampedIndex)
            return;

        const bool wasBlocked = blockSelectionCallback;
        blockSelectionCallback = true;

        selectedIndex = clampedIndex;
        repaint();

        blockSelectionCallback = wasBlocked;
    }

    [[nodiscard]] int getSelectedIndex() const
    {
        return selectedIndex;
    }

    [[nodiscard]] juce::String getSelectedText() const
    {
        if (selectedIndex >= 0 && selectedIndex < options.size())
            return options[selectedIndex];

        return {};
    }

    std::function<void(int)> onSelectionChanged;

    // ============================ Appearance API ============================

    void setCornerRadius(float newCornerRadius)
    {
        cornerRadius = std::max(0.0f, newCornerRadius);
        repaint();
    }

    void setDividerThickness(float newDividerThickness)
    {
        dividerThickness = std::max(0.0f, newDividerThickness);
        repaint();
    }

    void setFont(const juce::Font& newFont)
    {
        labelFont = newFont;
        repaint();
    }

    void setOrientation(Orientation newOrientation)
    {
        if (orientation == newOrientation)
            return;

        orientation = newOrientation;
        repaint();
    }

    [[nodiscard]] Orientation getOrientation() const
    {
        return orientation;
    }

    [[nodiscard]] bool isVertical() const
    {
        return orientation == Orientation::Vertical;
    }

    // ============================ JUCE Overrides ============================

    void paint(juce::Graphics& graphicsContext) override
    {
        const juce::Rectangle<int> bounds = getLocalBounds();

        const juce::Colour adjustedAccentGray =
            ThemeContext::GetAdjustedColour(AccentGray, *this);

        const juce::Colour adjustedFocusedGray =
            ThemeContext::GetAdjustedColour(FocusedGray, *this);

        const juce::Colour adjustedBGGray =
            ThemeContext::GetAdjustedColour(BGGray, *this);

        const juce::Colour adjustedUnfocusedGray =
            ThemeContext::GetAdjustedColour(UnfocusedGray, *this);

        if (options.isEmpty())
            return;

        const int numberOfOptions = options.size();
        const bool vertical = isVertical();

        const float totalMajor = vertical
            ? static_cast<float>(bounds.getHeight())
            : static_cast<float>(bounds.getWidth());

        const float totalMinor = vertical
            ? static_cast<float>(bounds.getWidth())
            : static_cast<float>(bounds.getHeight());

        const float segmentMajorFloat =
            totalMajor / static_cast<float>(numberOfOptions);

        for (int optionIndex = 0; optionIndex < numberOfOptions; ++optionIndex)
        {
            const float majorPosition =
                vertical
                    ? static_cast<float>(bounds.getY())
                        + std::floor(segmentMajorFloat * static_cast<float>(optionIndex))
                    : static_cast<float>(bounds.getX())
                        + std::floor(segmentMajorFloat * static_cast<float>(optionIndex));

            const float majorSize =
                (optionIndex == numberOfOptions - 1)
                    ? (vertical
                        ? static_cast<float>(bounds.getBottom()) - majorPosition
                        : static_cast<float>(bounds.getRight()) - majorPosition)
                    : std::floor(segmentMajorFloat);

            const juce::Rectangle<float> segmentBounds =
                vertical
                    ? juce::Rectangle<float>(
                        static_cast<float>(bounds.getX()),
                        majorPosition,
                        totalMinor,
                        majorSize)
                    : juce::Rectangle<float>(
                        majorPosition,
                        static_cast<float>(bounds.getY()),
                        majorSize,
                        totalMinor);

            const bool isFirst = (optionIndex == 0);
            const bool isLast = (optionIndex == numberOfOptions - 1);
            const bool isSelected = (optionIndex == selectedIndex);

            const juce::Colour fillColour =
                isSelected ? ThemePink : adjustedAccentGray;

            juce::Path segmentPath;

            if (vertical)
            {
                // top segment rounded on top, bottom segment rounded on bottom
                segmentPath.addRoundedRectangle(segmentBounds.getX(),
                                                segmentBounds.getY(),
                                                segmentBounds.getWidth(),
                                                segmentBounds.getHeight(),
                                                cornerRadius,
                                                cornerRadius,
                                                isFirst,
                                                isFirst,
                                                isLast,
                                                isLast);
            }
            else
            {
                // left segment rounded on left, right segment rounded on right
                segmentPath.addRoundedRectangle(segmentBounds.getX(),
                                                segmentBounds.getY(),
                                                segmentBounds.getWidth(),
                                                segmentBounds.getHeight(),
                                                cornerRadius,
                                                cornerRadius,
                                                isFirst,
                                                isLast,
                                                isFirst,
                                                isLast);
            }

            graphicsContext.setColour(fillColour);
            graphicsContext.fillPath(segmentPath);

            if (hoveredIndex == optionIndex && isEnabled())
            {
                graphicsContext.setColour(
                    adjustedFocusedGray.withMultipliedAlpha(0.10f));
                graphicsContext.fillPath(segmentPath);
            }

            if (!isLast && dividerThickness > 0.0f)
            {
                graphicsContext.setColour(adjustedBGGray.darker(0.2f));

                if (vertical)
                {
                    const float dividerY = segmentBounds.getBottom();

                    graphicsContext.fillRect(juce::Rectangle<float>(
                        segmentBounds.getX() + 2.0f,
                        dividerY - (dividerThickness * 0.5f),
                        segmentBounds.getWidth() - 4.0f,
                        dividerThickness));
                }
                else
                {
                    const float dividerX = segmentBounds.getRight();

                    graphicsContext.fillRect(juce::Rectangle<float>(
                        dividerX - (dividerThickness * 0.5f),
                        segmentBounds.getY() + 2.0f,
                        dividerThickness,
                        segmentBounds.getHeight() - 4.0f));
                }
            }

            graphicsContext.setColour(juce::Colours::white);
            graphicsContext.setFont(labelFont);

            const auto textBounds = segmentBounds.reduced(6.0f, 4.0f).toNearestInt();

            graphicsContext.drawFittedText(options[optionIndex],
                                           textBounds,
                                           juce::Justification::centred,
                                           1);
        }

        graphicsContext.setColour(adjustedUnfocusedGray.brighter(0.1f));
        graphicsContext.drawRoundedRectangle(bounds.toFloat().reduced(0.5f),
                                             cornerRadius,
                                             1.0f);
    }

    void mouseMove(const juce::MouseEvent& mouseEvent) override
    {
        const int newHoveredIndex = getIndexFromPosition(
            isVertical()
                ? static_cast<float>(mouseEvent.y)
                : static_cast<float>(mouseEvent.x));

        if (hoveredIndex != newHoveredIndex)
        {
            hoveredIndex = newHoveredIndex;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent& mouseEvent) override
    {
        juce::ignoreUnused(mouseEvent);
        hoveredIndex = -1;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& mouseEvent) override
    {
        if (!isEnabled() || options.isEmpty())
            return;

        const int clickedIndex = getIndexFromPosition(
            isVertical()
                ? static_cast<float>(mouseEvent.y)
                : static_cast<float>(mouseEvent.x));

        if (clickedIndex >= 0 && clickedIndex < options.size())
        {
            if (onGestureBegin != nullptr)
                onGestureBegin();

            if (onGestureCommit != nullptr)
                onGestureCommit(clickedIndex);

            setSelectedIndex(clickedIndex, juce::sendNotificationAsync);
        }
    }

    void mouseUp(const juce::MouseEvent& mouseEvent) override
    {
        juce::ignoreUnused(mouseEvent);

        if (!isEnabled())
            return;

        if (onGestureEnd != nullptr)
            onGestureEnd();
    }

    // ============================ Attachments ============================

    class ChoiceAttachment : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        ChoiceAttachment(juce::AudioProcessorValueTreeState& state,
                         juce::String parameterId,
                         SegmentedButton& segmentedControl)
            : apvts(state),
              parameterID(std::move(parameterId)),
              control(segmentedControl)
        {
            parameter = apvts.getParameter(parameterID);

            jassert(parameter != nullptr && "ChoiceAttachment: Parameter ID not found!");

            if (auto* choiceParameter =
                    dynamic_cast<juce::AudioParameterChoice*>(parameter))
            {
                if (control.getNumOptions() == 0)
                    control.setOptions(choiceParameter->choices);
            }

            control.onGestureBegin = [this]()
            {
                if (parameter != nullptr)
                    parameter->beginChangeGesture();
            };

            control.onGestureCommit = [this](int newIndex)
            {
                if (parameter != nullptr)
                {
                    const float rawTargetValue = static_cast<float>(newIndex);
                    const float normalised =
                        parameter->convertTo0to1(rawTargetValue);

                    parameter->setValueNotifyingHost(normalised);
                }
            };

            control.onGestureEnd = [this]()
            {
                if (parameter != nullptr)
                    parameter->endChangeGesture();
            };

            apvts.addParameterListener(parameterID, this);

            if (parameter != nullptr)
            {
                const float rawValue =
                    parameter->convertFrom0to1(parameter->getValue());

                control.setSelectedIndexSilently(
                    static_cast<int>(std::round(rawValue)));
            }
        }

        ~ChoiceAttachment() override
        {
            apvts.removeParameterListener(parameterID, this);

            control.onGestureBegin = nullptr;
            control.onGestureCommit = nullptr;
            control.onGestureEnd = nullptr;
        }

        void parameterChanged(const juce::String& changedParameterID,
                              float newValue) override
        {
            if (changedParameterID != parameterID)
                return;

            const int newIndex = static_cast<int>(std::round(newValue));

            juce::MessageManager::callAsync([this, newIndex]()
            {
                control.setSelectedIndexSilently(newIndex);
                control.repaint();
            });
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::String parameterID;
        juce::RangedAudioParameter* parameter = nullptr;
        SegmentedButton& control;
    };

    class ExclusiveBooleansAttachment : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        ExclusiveBooleansAttachment(juce::AudioProcessorValueTreeState& state,
                                    const std::vector<juce::String>& parameterIds,
                                    SegmentedButton& segmentedControl)
            : apvts(state),
              parameterIDs(parameterIds),
              control(segmentedControl)
        {
            jassert(control.getNumOptions() == static_cast<int>(parameterIDs.size())
                    && "ExclusiveBooleansAttachment: Option count must match parameter IDs count.");

            for (const auto& id : parameterIDs)
            {
                auto* parameter = apvts.getParameter(id);

                jassert(parameter != nullptr
                        && "ExclusiveBooleansAttachment: Parameter ID not found!");

                auto* boolParameter =
                    dynamic_cast<juce::AudioParameterBool*>(parameter);

                jassert(boolParameter != nullptr
                        && "ExclusiveBooleansAttachment: All parameters must be AudioParameterBool.");

                parameters.push_back(boolParameter);
                apvts.addParameterListener(id, this);
            }

            control.onGestureBegin = [this]()
            {
                for (auto* parameter : parameters)
                {
                    if (parameter != nullptr)
                        parameter->beginChangeGesture();
                }
            };

            control.onGestureCommit = [this](int newIndex)
            {
                if (ignoreParameterCallbacks)
                    return;

                ignoreParameterCallbacks = true;

                for (int parameterIndex = 0;
                     parameterIndex < static_cast<int>(parameters.size());
                     ++parameterIndex)
                {
                    auto* parameter = parameters[static_cast<size_t>(parameterIndex)];

                    if (parameter != nullptr)
                    {
                        const bool shouldBeOn = (parameterIndex == newIndex);
                        parameter->setValueNotifyingHost(shouldBeOn ? 1.0f : 0.0f);
                    }
                }

                ignoreParameterCallbacks = false;
            };

            control.onGestureEnd = [this]()
            {
                for (auto* parameter : parameters)
                {
                    if (parameter != nullptr)
                        parameter->endChangeGesture();
                }
            };

            int foundIndex = -1;

            for (int parameterIndex = 0;
                 parameterIndex < static_cast<int>(parameters.size());
                 ++parameterIndex)
            {
                if (parameters[static_cast<size_t>(parameterIndex)]->get())
                {
                    foundIndex = parameterIndex;
                    break;
                }
            }

            if (foundIndex < 0 && !parameters.empty())
            {
                ignoreParameterCallbacks = true;

                parameters[0]->beginChangeGesture();
                parameters[0]->setValueNotifyingHost(1.0f);

                for (size_t parameterIndex = 1;
                     parameterIndex < parameters.size();
                     ++parameterIndex)
                {
                    parameters[parameterIndex]->setValueNotifyingHost(0.0f);
                }

                parameters[0]->endChangeGesture();

                ignoreParameterCallbacks = false;
                foundIndex = 0;
            }

            control.setSelectedIndexSilently(foundIndex);
        }

        ~ExclusiveBooleansAttachment() override
        {
            for (const auto& id : parameterIDs)
                apvts.removeParameterListener(id, this);

            control.onGestureBegin = nullptr;
            control.onGestureCommit = nullptr;
            control.onGestureEnd = nullptr;
        }

        void parameterChanged(const juce::String& changedParameterID,
                              float newValue) override
        {
            if (ignoreParameterCallbacks)
                return;

            int selected = -1;

            if (newValue >= 0.5f)
            {
                for (size_t parameterIndex = 0;
                     parameterIndex < parameterIDs.size();
                     ++parameterIndex)
                {
                    if (parameterIDs[parameterIndex] == changedParameterID)
                    {
                        selected = static_cast<int>(parameterIndex);
                        break;
                    }
                }
            }
            else
            {
                for (size_t parameterIndex = 0;
                     parameterIndex < parameters.size();
                     ++parameterIndex)
                {
                    if (parameters[parameterIndex]->get())
                    {
                        selected = static_cast<int>(parameterIndex);
                        break;
                    }
                }
            }

            juce::MessageManager::callAsync([this, selected]()
            {
                control.setSelectedIndexSilently(selected);
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

    // ============================ Gesture Bridge ============================

    std::function<void()> onGestureBegin;
    std::function<void(int)> onGestureCommit;
    std::function<void()> onGestureEnd;

private:
    // ============================ Helpers ============================

    void initialiseFont()
    {
        auto fontOptions = juce::FontOptions("Liberation Sans",
                                             14.0f,
                                             juce::Font::bold);

        labelFont = juce::Font(fontOptions);
    }

    int getIndexFromPosition(float position) const
    {
        const int numberOfOptions = options.size();

        if (numberOfOptions <= 0)
            return -1;

        const float totalMajor = isVertical()
            ? static_cast<float>(getHeight())
            : static_cast<float>(getWidth());

        if (totalMajor <= 0.0f)
            return -1;

        const float segmentMajorFloat =
            totalMajor / static_cast<float>(numberOfOptions);

        int index = static_cast<int>(std::floor(
            juce::jlimit(0.0f, totalMajor - 1.0f, position)
            / segmentMajorFloat));

        return juce::jlimit(0, numberOfOptions - 1, index);
    }

    // ============================ State ============================

    juce::StringArray options;
    int selectedIndex = -1;
    int hoveredIndex = -1;

    float cornerRadius = 10.0f;
    float dividerThickness = 1.0f;
    juce::Font labelFont;

    bool blockSelectionCallback = false;
    Orientation orientation = Orientation::Horizontal;
};