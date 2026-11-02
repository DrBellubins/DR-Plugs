#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& ownerProcessor)
    : AudioProcessorEditor(&ownerProcessor), processorRef(ownerProcessor), webView(ownerProcessor)
{
    addAndMakeVisible(webView);
    setSize(760, 620);

    // The page owns the computer keyboard, so it needs the focus from the start
    // (standalone testing, and any DAW that hands keys to the editor).
    webView.setWantsKeyboardFocus(true);
    webView.grabKeyboardFocus();
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Nothing may be left sounding when the window goes away.
    webView.releaseAllNotes();
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Painted behind the web view: the page loads asynchronously, and this is
    // the colour it fades in from rather than a white flash.
    g.fillAll(juce::Colour(0xff323232));
}

void AudioPluginAudioProcessorEditor::resized() { webView.setBounds(getLocalBounds()); }
