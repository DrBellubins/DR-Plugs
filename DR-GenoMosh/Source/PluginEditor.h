#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Engine/Synth.h"
#include "PluginProcessor.h"
#include "UI/GeneGrid.h"
#include "UI/PlantView.h"
#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/ThemedKnob.h"

//==============================================================================
class AudioPluginAudioProcessorEditor  : public juce::AudioProcessorEditor
                                       , private juce::Timer
{
public:
    explicit AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (bool showGenes);

    AudioPluginAudioProcessor& processorRef;

    FlatRotaryLookAndFeel lookAndFeel;

    // Macro controls (design doc 4.1). The genome itself is edited on the
    // genes page, it is not a set of automatable parameters.
    ThemedKnob tuningKnob    { "tuning" };
    ThemedKnob atonalityKnob { "atonality" };
    ThemedKnob effectKnob    { "effect" };
    ThemedKnob rotationKnob  { "rotation" };

    juce::Slider volumeSlider, velSensSlider, wheelScaleSlider;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attachments;

    gmosh::PlantView plantView;
    gmosh::GeneGrid geneGrid;

    juce::Label title, pageLabel, wheelLabel;
    juce::TextButton newSeedButton { "new seed" };
    juce::TextButton plantBranchButton { "plant branch" };
    juce::TextButton pageButton { "genes" };

    bool showingGenes = false;
    int selectedBranch = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
