#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct RackTheme
{
    // Rack layout
    int rackPadding = 12;
    int moduleGap = 10;
    int moduleWidth = 260;

    // Module layout
    int modulePadding = 8;
    int moduleInnerGap = 10;
    int oscilloscopeHeight = 88;
    int optionRowY = 118;

    // Header
    int headerHeight = 18;
    int titleTopOffset = 8;

    // Scope
    int scopeSize = 64;

    // Controls
    int dropdownHeight = 22;
    int optionSize = 42;
    int optionSpacing = 10;
    int labelOffsetBelow = 4;

    // Enable button
    int enableButtonWidth = 14;
    int enableButtonHeight = 14;
    int enableButtonMargin = 2;

    // Corners / stroke
    float rackCornerRadius = 12.0f;
    float moduleCornerRadius = 8.0f;
    float rackOutlineThickness = 1.0f;
    float enableButtonCornerRadius = 4.0f;

    // Rack colours
    juce::Colour rackBackgroundColour = juce::Colour::fromRGB(10, 10, 10);

    // Theme derivation
    float rackOutlineBrightenAmount = 0.0f;
    float moduleBackgroundDarkenAmount = 1.3f;
    float moduleOutlineBrightenAmount = 0.2f;
    float moduleLabelDarkenAmount = 0.15f;
    float moduleControlDarkenAmount = 0.35f;

    float controlUnselectedDarkenAmount = 1.45f;
    float controlOutlineDarkenAmount = 0.35f;

    // Disabled-state presentation
    float disabledAlpha = 0.2f;
};