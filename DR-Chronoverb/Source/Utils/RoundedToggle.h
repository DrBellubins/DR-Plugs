#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

// RoundedToggle
// - A themed, rounded toggle switch supporting horizontal or vertical orientation.
// - Thumb is circular and uses ThemePink.
// - Enabled trail uses ThemePink.darker(0.2f).
// - Disabled trail uses AccentGray.
// - Provides smooth animation between states.
// - Supports attachment to an AudioProcessorValueTreeState boolean parameter (Attachment helper).
// - Exposes gesture callbacks (onGestureBegin / onGestureEnd) similar to SegmentedButton.
//
// Usage (standalone):
//     auto* Toggle = new RoundedToggle();
//     Toggle->setOrientation(RoundedToggle::Orientation::Horizontal);
//     Toggle->onStateChanged = [](bool IsOn) { DBG("Toggle is now " << (IsOn ? "On" : "Off")); };
//
// Usage (parameter attachment):
//     roundedToggleAttachment = std::make_unique<RoundedToggle::Attachment>(apvts, "enableFeature", *Toggle);
//
// Animation:
// - The internal animated position (AnimationPosition) interpolates toward the logical state (ToggleState).
// - Call setAnimationSpeed() to adjust responsiveness.
//
// Accessibility / Keyboard:
// - Space / Return toggles state when the component has focus.
// - setWantsKeyboardFocus(true) is enabled by default.
//
class RoundedToggle : public juce::Component, private juce::Timer
{
public:
    enum class Orientation
    {
        Horizontal,
        Vertical
    };

    RoundedToggle()
    {
        setWantsKeyboardFocus(true);
        setInterceptsMouseClicks(true, true);
    }

    ~RoundedToggle() override = default;

    // ----------------------------- State API -----------------------------
    void setState(bool NewState, juce::NotificationType NotificationType = juce::sendNotificationAsync)
    {
        if (ToggleState == NewState)
            return;

        ToggleState = NewState;

        if (!IsAnimating)
        {
            startTimerHz(AnimationTimerFrequencyHz);
            IsAnimating = true;
        }

        if (NotificationType == juce::sendNotificationAsync || NotificationType == juce::sendNotification)
        {
            if (onStateChanged != nullptr)
                onStateChanged(ToggleState);
        }

        repaint();
    }

    // Set state silently (no callback).
    void setStateSilently(bool NewState)
    {
        if (ToggleState == NewState)
            return;

        ToggleState = NewState;

        if (!IsAnimating)
        {
            startTimerHz(AnimationTimerFrequencyHz);
            IsAnimating = true;
        }

        repaint();
    }

    bool getState() const
    {
        return ToggleState;
    }

    // ----------------------------- Orientation API -----------------------------
    void setOrientation(RoundedToggle::Orientation NewOrientation)
    {
        if (ToggleOrientation == NewOrientation)
            return;

        ToggleOrientation = NewOrientation;
        repaint();
    }

    Orientation getOrientation() const
    {
        return ToggleOrientation;
    }

    // ----------------------------- Appearance API -----------------------------
    void setTrackPadding(float NewPaddingPixels)
    {
        TrackPaddingPixels = std::max(0.0f, NewPaddingPixels);
        repaint();
    }

    void setAnimationSpeed(float NewSpeed)
    {
        AnimationSmoothingCoefficient = juce::jlimit(0.001f, 1.0f, NewSpeed);
    }

    void setThumbShadowEnabled(bool ShouldEnable)
    {
        ThumbShadowEnabled = ShouldEnable;
        repaint();
    }

    void setTrackCornerRadius(float NewTrackCornerRadius)
    {
        TrackCornerRadius = (NewTrackCornerRadius < 0.0f ? -1.0f : NewTrackCornerRadius);
        repaint();
    }

    void setThumbCornerRadius(float NewThumbCornerRadius)
    {
        ThumbCornerRadius = (NewThumbCornerRadius < 0.0f ? -1.0f : NewThumbCornerRadius);
        repaint();
    }

    // ----------------------------- Callbacks -----------------------------
    std::function<void(bool)> onStateChanged;
    std::function<void()> onGestureBegin;
    std::function<void()> onGestureEnd;

    // ----------------------------- JUCE Overrides -----------------------------
    void paint(juce::Graphics& GraphicsContext) override
    {
        const juce::Rectangle<int> LocalBounds = getLocalBounds();

        if (LocalBounds.getWidth() <= 2 || LocalBounds.getHeight() <= 2)
            return;

        juce::Rectangle<float> TrackBounds = LocalBounds.toFloat().reduced(TrackPaddingPixels);

        const bool IsHorizontal = (ToggleOrientation == Orientation::Horizontal);
        const float MinorAxis = IsHorizontal ? TrackBounds.getHeight() : TrackBounds.getWidth();
        const float TrackThickness = MinorAxis * 0.55f;

        if (IsHorizontal)
        {
            const float CenterY = TrackBounds.getCentreY();
            TrackBounds = juce::Rectangle<float>(TrackBounds.getX(),
                                                 CenterY - TrackThickness * 0.5f,
                                                 TrackBounds.getWidth(),
                                                 TrackThickness);
        }
        else
        {
            const float CenterX = TrackBounds.getCentreX();
            TrackBounds = juce::Rectangle<float>(CenterX - TrackThickness * 0.5f,
                                                 TrackBounds.getY(),
                                                 TrackThickness,
                                                 TrackBounds.getHeight());
        }

        const float AutoTrackCornerRadius = TrackBounds.getHeight() * 0.5f;
        const float EffectiveTrackCornerRadius = (TrackCornerRadius >= 0.0f ? TrackCornerRadius : AutoTrackCornerRadius);

        // Animation position and thumb geometry for tail positioning
        const float AnimatedValue = AnimationPosition;

        const float ThumbWidth = TrackThickness * 1.5f;
        const float ThumbHeight = ThumbWidth;

        juce::Point<float> ThumbCenter;

        if (IsHorizontal)
        {
            const float LeftX = TrackBounds.getX() + ThumbWidth * 0.5f;
            const float RightX = TrackBounds.getRight() - ThumbWidth * 0.5f;
            const float X = juce::jmap(AnimatedValue, 0.0f, 1.0f, LeftX, RightX);
            const float Y = TrackBounds.getCentreY();
            ThumbCenter = { X, Y };
        }
        else
        {
            const float BottomY = TrackBounds.getBottom() - ThumbHeight * 0.5f;
            const float TopY = TrackBounds.getY() + ThumbHeight * 0.5f;
            const float Y = juce::jmap(AnimatedValue, 0.0f, 1.0f, BottomY, TopY);
            const float X = TrackBounds.getCentreX();
            ThumbCenter = { X, Y };
        }

        // Build the rounded track path once
        juce::Path TrackPath;
        TrackPath.addRoundedRectangle(TrackBounds, EffectiveTrackCornerRadius);

        // Base track (unfilled portion)
        GraphicsContext.setColour(AccentGray);
        GraphicsContext.fillPath(TrackPath);

        // Animated pink tail: clip to follow the thumb position, then fill same rounded track path
        {
            juce::Graphics::ScopedSaveState ScopedState(GraphicsContext);

            if (IsHorizontal)
            {
                const float TailRight = juce::jlimit(TrackBounds.getX(), TrackBounds.getRight(), ThumbCenter.x);
                const juce::Rectangle<float> TailClipBounds(TrackBounds.getX(),
                                                            TrackBounds.getY(),
                                                            TailRight - TrackBounds.getX(),
                                                            TrackBounds.getHeight());
                GraphicsContext.reduceClipRegion(TailClipBounds.toNearestInt());
            }
            else
            {
                const float TailTop = juce::jlimit(TrackBounds.getY(), TrackBounds.getBottom(), ThumbCenter.y);
                const juce::Rectangle<float> TailClipBounds(TrackBounds.getX(),
                                                            TailTop,
                                                            TrackBounds.getWidth(),
                                                            TrackBounds.getBottom() - TailTop);
                GraphicsContext.reduceClipRegion(TailClipBounds.toNearestInt());
            }

            GraphicsContext.setColour(ThemePink.darker(0.2f));
            GraphicsContext.fillPath(TrackPath);
        }

        // Outline
        GraphicsContext.setColour(UnfocusedGray.brighter(0.1f));
        GraphicsContext.strokePath(TrackPath, juce::PathStrokeType(1.0f));

        // Thumb
        juce::Rectangle<float> ThumbBounds(ThumbCenter.x - ThumbWidth * 0.5f,
                                           ThumbCenter.y - ThumbHeight * 0.5f,
                                           ThumbWidth,
                                           ThumbHeight);

        const float AutoThumbCornerRadius = ThumbWidth * 0.5f;
        const float EffectiveThumbCornerRadius = (ThumbCornerRadius >= 0.0f ? ThumbCornerRadius : AutoThumbCornerRadius);

        if (ThumbShadowEnabled)
        {
            juce::DropShadow Shadow(juce::Colours::black.withAlpha(0.50f),
                                    static_cast<int>(std::ceil(ThumbWidth * 0.10f)),
                                    juce::Point<int>(0, 2));
            Shadow.drawForRectangle(GraphicsContext, ThumbBounds.toNearestInt());
        }

        juce::Path ThumbPath;
        ThumbPath.addRoundedRectangle(ThumbBounds, EffectiveThumbCornerRadius);

        GraphicsContext.setColour(ThemePink);
        GraphicsContext.fillPath(ThumbPath);
    }

    void mouseDown(const juce::MouseEvent& MouseEvent) override
    {
        juce::ignoreUnused(MouseEvent);

        if (!isEnabled())
            return;

        if (onGestureBegin != nullptr)
            onGestureBegin();

        // Toggle state.
        setState(!ToggleState, juce::sendNotificationAsync);
    }

    void mouseUp(const juce::MouseEvent& MouseEvent) override
    {
        juce::ignoreUnused(MouseEvent);

        if (!isEnabled())
            return;

        if (onGestureEnd != nullptr)
            onGestureEnd();
    }

    bool keyPressed(const juce::KeyPress& KeyPressEvent) override
    {
        if (KeyPressEvent == juce::KeyPress::spaceKey || KeyPressEvent == juce::KeyPress::returnKey)
        {
            if (onGestureBegin != nullptr)
                onGestureBegin();

            setState(!ToggleState, juce::sendNotificationAsync);

            if (onGestureEnd != nullptr)
                onGestureEnd();

            return true;
        }

        return false;
    }

private:
    // ----------------------------- Animation -----------------------------
    void timerCallback() override
    {
        // Smooth animation toward target state.
        const float TargetValue = (ToggleState ? 1.0f : 0.0f);

        AnimationPosition = AnimationPosition + AnimationSmoothingCoefficient * (TargetValue - AnimationPosition);

        // Close enough: stop.
        if (std::abs(AnimationPosition - TargetValue) < 0.0005f)
        {
            AnimationPosition = TargetValue;
            stopTimer();
            IsAnimating = false;
        }

        repaint();
    }

    // ----------------------------- Internal State -----------------------------
    Orientation ToggleOrientation = Orientation::Horizontal;
    bool ToggleState = false;

    float AnimationPosition = 0.0f;
    float AnimationSmoothingCoefficient = 0.2f;
    bool IsAnimating = false;
    int AnimationTimerFrequencyHz = 60;

    float TrackPaddingPixels = 4.0f;
    bool ThumbShadowEnabled = true;

    float TrackCornerRadius = 5.0f; // <0 => auto (half height)
    float ThumbCornerRadius = 5.0f; // <0 => auto (circle)

public:
    // ----------------------------- Attachment Helper -----------------------------
    // Binds the toggle to an AudioProcessorValueTreeState boolean parameter.
    class Attachment : public juce::AudioProcessorValueTreeState::Listener
    {
    public:
        Attachment(juce::AudioProcessorValueTreeState& State,
                   const juce::String& ParameterID,
                   RoundedToggle& ToggleRef)
            : apvts(State),
              parameterID(ParameterID),
              toggle(ToggleRef)
        {
            parameter = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter(parameterID));
            jassert(parameter != nullptr && "RoundedToggle::Attachment: Parameter ID must refer to an AudioParameterBool.");

            // Wire toggle -> parameter gesture.
            toggle.onGestureBegin = [this]()
            {
                if (parameter != nullptr)
                    parameter->beginChangeGesture();
            };

            toggle.onGestureEnd = [this]()
            {
                if (parameter != nullptr)
                    parameter->endChangeGesture();
            };

            toggle.onStateChanged = [this](bool IsOn)
            {
                if (ignoreCallbacks)
                    return;

                if (parameter != nullptr)
                {
                    const float NormalisedValue = IsOn ? 1.0f : 0.0f;
                    parameter->setValueNotifyingHost(NormalisedValue);
                }
            };

            apvts.addParameterListener(parameterID, this);

            // Initial sync.
            if (parameter != nullptr)
                toggle.setStateSilently(parameter->get());
        }

        ~Attachment() override
        {
            apvts.removeParameterListener(parameterID, this);
            toggle.onGestureBegin = nullptr;
            toggle.onGestureEnd = nullptr;
            toggle.onStateChanged = nullptr;
        }

        void parameterChanged(const juce::String& ChangedParameterID, float NewValue) override
        {
            if (ChangedParameterID != parameterID)
                return;

            const bool NewState = (NewValue >= 0.5f);

            juce::MessageManager::callAsync([this, NewState]()
            {
                ignoreCallbacks = true;
                toggle.setStateSilently(NewState);
                ignoreCallbacks = false;
            });
        }

    private:
        juce::AudioProcessorValueTreeState& apvts;
        juce::String parameterID;
        juce::AudioParameterBool* parameter = nullptr;
        RoundedToggle& toggle;
        std::atomic<bool> ignoreCallbacks { false };
    };
};