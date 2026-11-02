#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Web/EditorWebView.h"

//==============================================================================
/// The editor is a host for the web UI (WebUI/, embedded in the binary). All of
/// the widget behaviour lives in the page; all of the sound, the genome and the
/// plant live in the processor. This class only owns the view and makes sure
/// nothing is left sounding when the window closes.
class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor
{
  public:
    explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

  private:
    AudioPluginAudioProcessor& processorRef;
    gmosh::EditorWebView webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
