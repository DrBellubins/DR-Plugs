#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/Synth.h"
#include "PluginProcessor.h"
#include "UI/ComputerKeyboard.h"
#include "UI/GeneGrid.h"
#include "UI/PlantView.h"
#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/ThemedKnob.h"

//==============================================================================
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
  public:
    explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    void timerCallback() override;
    void showPage(bool showGenes);

    /// The one place the geometry lives, so paint() can draw the live control
    /// panels exactly where resized() puts the widgets.
    struct Layout
    {
        juce::Rectangle<int> header;
        juce::Rectangle<int> footerBand, footerPanel, footer;
        juce::Rectangle<int> body, plant;
        juce::Rectangle<int> controlsPanel, knobRowTop, knobRowBottom, macroPlate;
    };
    Layout layout() const;

    // Computer keyboard playing (ported from the Chronoverb keyboard map).
    // These are the Component key hooks: JUCE walks them from the focused
    // component up to the editor, so they work with a slider focused too.
    bool keyPressed(const juce::KeyPress&) override;
    bool keyStateChanged(bool isKeyDown) override;
    void updateKeyHint();

    AudioPluginAudioProcessor& processorRef;

    FlatRotaryLookAndFeel lookAndFeel;

    // Macro controls (design doc 4.1). The genome itself is edited on the
    // genes page, it is not a set of automatable parameters.
    ThemedKnob tuningKnob{ "tuning" };
    ThemedKnob atonalityKnob{ "atonality" };
    ThemedKnob effectKnob{ "effect" };

    juce::Slider volumeSlider, velSensSlider, wheelScaleSlider;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attachments;
    gmosh::PlantView plantView;
    gmosh::GeneGrid geneGrid;
    gmosh::ComputerKeyboard computerKeys;

    juce::Label title, pageLabel, wheelLabel, keyHint;
    int shownOctave = 0;

    // Captions: the bottom sliders are macros, but nothing on screen said so.
    juce::Label volumeLabel, velSensLabel, wheelScaleLabel;
    juce::Label liveCaption, outputCaption;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    juce::TextButton newSeedButton{ "new seed" };
    juce::TextButton plantBranchButton{ "plant branch" };
    juce::TextButton pageButton{ "genes" };

    bool showingGenes = false;
    int selectedBranch = -1;

    enum
    {
        kHeaderHeight = 44,
        kFooterHeight = 104,
        kSideColumnWidth = 270,
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
