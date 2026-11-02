#include "EditorWebView.h"

#include "../PluginProcessor.h"
#include "PlantGeometry.h"
#include "WebAssets.h"

#include <algorithm>

namespace gmosh
{
namespace
{
//==============================================================================
// WebUI/ files as embedded resources. The names come from the CMake binary data
// target (juce_add_binary_data), one symbol per file.
struct Asset
{
    const char* name;
    const char* mimeType;
    const char* data;
    int size;
};

const Asset assets[] = {
    { "index.html", "text/html; charset=utf-8", gmosh::WebAssets::index_html,
      gmosh::WebAssets::index_htmlSize },
    { "style.css", "text/css; charset=utf-8", gmosh::WebAssets::style_css, gmosh::WebAssets::style_cssSize },
    { "app.js", "text/javascript; charset=utf-8", gmosh::WebAssets::app_js, gmosh::WebAssets::app_jsSize },
    { "plant.js", "text/javascript; charset=utf-8", gmosh::WebAssets::plant_js,
      gmosh::WebAssets::plant_jsSize },
    { "bridge.js", "text/javascript; charset=utf-8", gmosh::WebAssets::bridge_js,
      gmosh::WebAssets::bridge_jsSize },
};
} // namespace

//==============================================================================
const juce::StringArray& EditorWebView::editableParameterIds()
{
    static const juce::StringArray ids{ "volume", "velSens", "tuning", "atonality", "effect", "wheelScale" };
    return ids;
}

//==============================================================================
EditorWebView::EditorWebView(AudioPluginAudioProcessor& ownerProcessor)
    : WebBrowserComponent(
          juce::WebBrowserComponent::Options{}
              .withNativeIntegrationEnabled()
              .withResourceProvider([this](const juce::String& url) { return resourceFor(url); })
              .withEventListener("ui.ready", [this](const juce::var&) { handleReady(); })
              .withEventListener("ui.param", [this](const juce::var& payload) { handleParam(payload); })
              .withEventListener("ui.gene", [this](const juce::var& payload) { handleGene(payload); })
              .withEventListener("ui.note", [this](const juce::var& payload) { handleNote(payload); })
              .withEventListener("ui.key", [this](const juce::var& payload) { handleKey(payload); })
              .withEventListener("ui.newSeed", [this](const juce::var&) { handleNewSeed(); })
              .withEventListener("ui.plantBranch",
                                 [this](const juce::var& payload) { handlePlantBranch(payload); })
              .withEventListener("ui.requestGenes", [this](const juce::var&) { sendGenes(); })
              .withEventListener("ui.error",
                                 [](const juce::var& payload)
                                 {
                                     // An exception thrown inside the page is otherwise
                                     // invisible: the back end never sees it.
                                     std::cerr << "[gmosh-web] " << payload["message"].toString().toRawUTF8()
                                               << std::endl;
                                 })),
      processorRef(ownerProcessor)
{
    computerKeys.onNote = [this](int midiNote, float velocity)
    {
        if (velocity > 0.0f)
            processorRef.playNote(midiNote, velocity);
        else
            processorRef.stopNote(midiNote);
    };

    goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz(30);
}

EditorWebView::~EditorWebView()
{
    stopTimer();
    computerKeys.releaseAll();
}

//==============================================================================
void EditorWebView::releaseAllNotes() { computerKeys.releaseAll(); }

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource> EditorWebView::resourceFor(const juce::String& url)
{
    const juce::String requested =
        url == "/" ? juce::String("index.html") : url.fromFirstOccurrenceOf("/", false, false);

    for (const auto& asset : assets)
    {
        if (requested.isNotEmpty() && requested.compareIgnoreCase(asset.name) == 0)
        {
            const auto* first = reinterpret_cast<const std::byte*>(asset.data);

            return juce::WebBrowserComponent::Resource{
                std::vector<std::byte>(first, first + static_cast<std::size_t>(asset.size)), asset.mimeType
            };
        }
    }

    return std::nullopt;
}

//==============================================================================
juce::AudioParameterFloat* EditorWebView::parameter(const juce::String& parameterId) const
{
    return dynamic_cast<juce::AudioParameterFloat*>(processorRef.parameters.getParameter(parameterId));
}

//==============================================================================
float EditorWebView::normalisedValue(const juce::String& parameterId) const
{
    auto* target = parameter(parameterId);
    return target == nullptr ? 0.0f : target->convertTo0to1(target->get());
}

//==============================================================================
void EditorWebView::sendControls()
{
    juce::Array<juce::var> controls;

    for (const auto& parameterId : editableParameterIds())
    {
        auto* target = parameter(parameterId);

        if (target == nullptr)
            continue;

        const auto& range = target->getNormalisableRange();

        auto* control = new juce::DynamicObject();
        control->setProperty("id", parameterId);
        control->setProperty("label", juce::String(target->getLabel()));
        control->setProperty("min", static_cast<double>(range.start));
        control->setProperty("max", static_cast<double>(range.end));
        controls.add(juce::var(control));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty("controls", juce::var(controls));
    emitEventIfBrowserIsVisible("ui.controls", juce::var(root));
}

//==============================================================================
void EditorWebView::sendState()
{
    auto* parameters = new juce::DynamicObject();

    for (const auto& parameterId : editableParameterIds())
        parameters->setProperty(parameterId, static_cast<double>(normalisedValue(parameterId)));

    juce::Array<juce::var> lengths;

    for (int branchIndex = 0; branchIndex < kNumBranches; ++branchIndex)
        lengths.add(
            static_cast<double>(normalisedValue(AudioPluginAudioProcessor::branchParamId(branchIndex))));

    auto* root = new juce::DynamicObject();
    root->setProperty("params", juce::var(parameters));
    root->setProperty("lengths", juce::var(lengths));
    root->setProperty("wheel", static_cast<double>(processorRef.currentWheel()));
    root->setProperty("wheelScale", static_cast<double>(normalisedValue("wheelScale")));
    root->setProperty("active", processorRef.lastPlayedBranch());
    root->setProperty("seedId", juce::String(processorRef.seedId()));
    root->setProperty("octave", computerKeys.octave());

    // 30 Hz for a value that is usually unchanged would spend the whole budget
    // on the same message; the serialisation doubles as the comparison. One var
    // owns the object: wrapping the raw pointer twice would delete it in the
    // first temporary.
    const juce::var stateObject(root);
    const juce::String encoded = juce::JSON::toString(stateObject, true);

    if (encoded == lastStateSignature)
        return;

    lastStateSignature = encoded;
    emitEventIfBrowserIsVisible("ui.state", stateObject);
}

//==============================================================================
void EditorWebView::sendPlant()
{
    emitEventIfBrowserIsVisible("ui.plant", PlantGeometry::toJson(processorRef.lastPlantSnapshot()));
}

//==============================================================================
void EditorWebView::sendGenes()
{
    const Genome& seed = processorRef.seedGenome();

    juce::Array<juce::var> groups;

    for (int groupIndex = 0; groupIndex < GrpCount; ++groupIndex)
        groups.add(juce::String(kGroupNames[static_cast<std::size_t>(groupIndex)]));

    juce::Array<juce::var> genes;

    for (std::size_t geneIndex = 0; geneIndex < kNumGenes; ++geneIndex)
    {
        auto* gene = new juce::DynamicObject();
        gene->setProperty("i", static_cast<int>(geneIndex));
        gene->setProperty("label", juce::String(kGenes[geneIndex].label));
        gene->setProperty("g", static_cast<int>(kGenes[geneIndex].group));
        gene->setProperty("v", static_cast<double>(seed[geneIndex]));
        genes.add(juce::var(gene));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty("groups", juce::var(groups));
    root->setProperty("genes", juce::var(genes));
    emitEventIfBrowserIsVisible("ui.genes", juce::var(root));
}

//==============================================================================
void EditorWebView::sendKeyHint()
{
    auto* root = new juce::DynamicObject();
    root->setProperty("hint", ComputerKeyboard::helpText());
    emitEventIfBrowserIsVisible("ui.keys", juce::var(root));
}

//==============================================================================
void EditorWebView::pageFinishedLoading(const juce::String& url)
{
    WebBrowserComponent::pageFinishedLoading(url);

    // The page asks for everything on ui.ready; pushing here as well would race
    // with its own listeners being installed.
}

//==============================================================================
void EditorWebView::handleReady()
{
    // A fresh page has heard nothing: the change detection has to start over,
    // or a state message sent before the page existed would never be repeated.
    pageIsReady = true;
    lastStateSignature.clear();

    sendControls();
    sendGenes();
    sendPlant();
    sendKeyHint();
    sendState();

    lastPlantRevision = processorRef.lastPlantSnapshot().revision;
    lastSeedId = processorRef.seedId();
    lastOctave = computerKeys.octave();
}

//==============================================================================
void EditorWebView::handleParam(const juce::var& payload)
{
    const juce::String parameterId = payload["id"].toString();
    const double value = payload["value"];

    if (parameterId.isEmpty())
        return;

    auto* target = parameter(parameterId);

    if (target != nullptr)
        target->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, static_cast<float>(value)));
}

//==============================================================================
void EditorWebView::handleGene(const juce::var& payload)
{
    const int geneIndex = payload["index"];

    if (geneIndex < 0 || geneIndex >= static_cast<int>(kNumGenes))
        return;

    processorRef.setSeedGene(static_cast<std::size_t>(geneIndex),
                             static_cast<float>(static_cast<double>(payload["value"])));
}

//==============================================================================
void EditorWebView::handleNote(const juce::var& payload)
{
    const int midiNote = payload["note"];
    const float velocity = static_cast<float>(static_cast<double>(payload["velocity"]));

    if (midiNote < 0 || midiNote > 127)
        return;

    if (velocity > 0.0f)
        processorRef.playNote(midiNote, velocity);
    else
        processorRef.stopNote(midiNote);
}

//==============================================================================
void EditorWebView::handleKey(const juce::var& payload)
{
    const juce::String character = payload["ch"].toString();

    if (character.length() != 1)
        return;

    // The map in ComputerKeyboard is the only key map in the instrument; the
    // page sends the typed character and this class does the rest.
    const int keyCode = static_cast<int>(character[0]);

    if (static_cast<bool>(payload["down"]))
        computerKeys.handleKeyPress(juce::KeyPress(keyCode));
    else
        computerKeys.releaseKey(keyCode);

    if (computerKeys.octave() != lastOctave)
    {
        lastOctave = computerKeys.octave();
        sendKeyHint();
    }
}

//==============================================================================
void EditorWebView::handleNewSeed()
{
    processorRef.newSeed();
    sendPlant();
    sendGenes();

    lastPlantRevision = processorRef.lastPlantSnapshot().revision;
    lastSeedId = processorRef.seedId();
}

//==============================================================================
void EditorWebView::handlePlantBranch(const juce::var& payload)
{
    const int branchIndex = payload["branch"];

    if (branchIndex < 0 || branchIndex >= kNumBranches)
        return;

    const auto* target = parameter(AudioPluginAudioProcessor::branchParamId(branchIndex));
    processorRef.plantBranch(branchIndex, target == nullptr ? 0.0f : target->get());

    sendPlant();
    sendGenes();

    lastPlantRevision = processorRef.lastPlantSnapshot().revision;
    lastSeedId = processorRef.seedId();
}

//==============================================================================
void EditorWebView::timerCallback()
{
    if (!pageIsReady)
        return;

    const PlantSnapshot& snapshot = processorRef.lastPlantSnapshot();

    // Growing, planting and re-seeding all change the snapshot revision, so one
    // check covers every way the picture can change.
    if (snapshot.revision != lastPlantRevision || processorRef.seedId() != lastSeedId)
    {
        lastPlantRevision = snapshot.revision;
        lastSeedId = processorRef.seedId();
        sendPlant();
        sendGenes();
    }

    sendState();
}

} // namespace gmosh
