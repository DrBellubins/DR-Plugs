#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

class DarkeningThemeProvider
{
public:
    virtual ~DarkeningThemeProvider() = default;

    virtual float GetThemeDarkeningAmount() const = 0;
};

namespace ThemeContext
{
    inline float GetDarkeningAmountForComponent(const juce::Component& component)
    {
        const juce::Component* currentComponent = &component;

        while (currentComponent != nullptr)
        {
            const auto* darkeningThemeProvider =
                dynamic_cast<const DarkeningThemeProvider*>(currentComponent);

            if (darkeningThemeProvider != nullptr)
            {
                return darkeningThemeProvider->GetThemeDarkeningAmount();
            }

            currentComponent = currentComponent->getParentComponent();
        }

        return 0.0f;
    }

    inline juce::Colour GetAdjustedColour(
        const juce::Colour& baseColour,
        const juce::Component& component)
    {
        if (baseColour == ThemePink)
        {
            return baseColour;
        }

        const float darkeningAmount = GetDarkeningAmountForComponent(component);
        return baseColour.darker(darkeningAmount);
    }
}