#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "../Genetic/PlantEngine.h"
#include "../UI/ComputerKeyboard.h"

class AudioPluginAudioProcessor;

namespace gmosh
//==============================================================================
// The web editor surface.
//
// The whole front end is HTML/CSS/JS (WebUI/, embedded by CMake and served from
// the binary through the resource provider); this class is the only bridge. It
// owns no state the audio engine does not already have: it publishes parameters,
// branch lengths, the live wheel and the plant geometry, and it turns messages
// from the page back into processor calls.
//
// Message thread only. Nothing here touches the audio thread except through the
// processor's own thread safe entry points.
//==============================================================================
{
class EditorWebView : public juce::WebBrowserComponent, private juce::Timer
{
  public:
    explicit EditorWebView(AudioPluginAudioProcessor& ownerProcessor);
    ~EditorWebView() override;

    /// Note off for everything the page or the computer keyboard is holding.
    void releaseAllNotes();

  private:
    //==============================================================================
    // Served from BinaryData, so the plug-in has no files to find at runtime.
    std::optional<juce::WebBrowserComponent::Resource> resourceFor(const juce::String& url);

    //==============================================================================
    // Messages from the page. Payloads arrive as parsed JSON.
    void handleReady();
    void handleParam(const juce::var& payload);
    void handleGene(const juce::var& payload);
    void handleNote(const juce::var& payload);
    void handleKey(const juce::var& payload);
    void handleNewSeed();
    void handlePlantBranch(const juce::var& payload);

    //==============================================================================
    // State to the page.
    void sendControls();
    void sendState();
    void sendPlant();
    void sendGenes();
    void sendKeyHint();

    /// The parameters the page may edit. The branch lengths and the wheel are in
    /// the state message instead: they belong to the plant, not to the control
    /// strip.
    static const juce::StringArray& editableParameterIds();

    juce::AudioParameterFloat* parameter(const juce::String& parameterId) const;
    float normalisedValue(const juce::String& parameterId) const;

    void timerCallback() override;
    void pageFinishedLoading(const juce::String& url) override;

    AudioPluginAudioProcessor& processorRef;
    ComputerKeyboard computerKeys;

    bool pageIsReady = false;
    int lastPlantRevision = -1;
    uint64_t lastSeedId = 0;
    juce::String lastStateSignature;
    int lastOctave = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorWebView)
};

} // namespace gmosh
