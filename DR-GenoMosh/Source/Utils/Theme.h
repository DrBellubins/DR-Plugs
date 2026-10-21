#pragma once

#include <juce_graphics/juce_graphics.h> // Only JUCE color types needed

const juce::Colour BGGray = juce::Colour(50, 50, 50);
const juce::Colour AccentGray = juce::Colour(40, 40, 40);
const juce::Colour FocusedGray = juce::Colour(120, 120, 120);
const juce::Colour UnfocusedGray = juce::Colour(30, 30, 30);
const juce::Colour ThemePink = juce::Colour(255, 140, 230);

// Panels: a darker plate under the controls that can be moved while the synth
// is sounding, so the seed side of the UI and the live side read as two
// different things at a glance.
const juce::Colour PanelGray = juce::Colour(38, 38, 38);
const juce::Colour PanelEdge = juce::Colour(64, 64, 64);
const juce::Colour CaptionGray = juce::Colour(145, 145, 145);