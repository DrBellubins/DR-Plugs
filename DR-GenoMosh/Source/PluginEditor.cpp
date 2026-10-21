#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Utils/Theme.h"

//==============================================================================
namespace
{
    juce::AudioParameterFloat* param (AudioPluginAudioProcessor& processor, const juce::String& id)
    {
        return dynamic_cast<juce::AudioParameterFloat*> (processor.parameters.getParameter (id));
    }
}

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& ownerProcessor)
    : AudioProcessorEditor (&ownerProcessor), processorRef (ownerProcessor)
{
    setLookAndFeel (&lookAndFeel);

    // The editor itself takes key focus so the computer keyboard plays the
    // synth even when nothing else is focused (standalone testing, no DAW).
    setWantsKeyboardFocus (true);

    computerKeys.onNote = [this] (int note, float velocity)
    {
        if (velocity > 0.0f)
            processorRef.playNote (note, velocity);
        else
            processorRef.stopNote (note);
    };

    auto attach = [this] (const juce::String& id, juce::Slider& slider)
    {
        if (auto* p = param (processorRef, id))
        {
            const auto& range = p->getNormalisableRange();
            slider.setRange (static_cast<double> (range.start),
                             static_cast<double> (range.end));
            slider.setValue (static_cast<double> (p->get()), juce::dontSendNotification);
            attachments.push_back (std::make_unique<juce::SliderParameterAttachment> (*p, slider, nullptr));
        }
    };

    attach ("volume", volumeSlider);
    attach ("velSens", velSensSlider);
    attach ("wheelScale", wheelScaleSlider);
    attach ("tuning", tuningKnob);
    attach ("atonality", atonalityKnob);
    attach ("effect", effectKnob);
    attach ("rotation", rotationKnob);

    for (juce::Slider* s : { &volumeSlider, &velSensSlider, &wheelScaleSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 18);
        addAndMakeVisible (s);
    }

    for (juce::Slider* s : { &tuningKnob, &atonalityKnob, &effectKnob, &rotationKnob })
        addAndMakeVisible (s);

    title.setText ("GENOMOSH", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (21.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
    addAndMakeVisible (title);

    pageLabel.setText ("seed / branches", juce::dontSendNotification);
    addAndMakeVisible (pageLabel);

    wheelLabel.setText ("wheel", juce::dontSendNotification);
    addAndMakeVisible (wheelLabel);

    addAndMakeVisible (newSeedButton);
    addAndMakeVisible (plantBranchButton);
    addAndMakeVisible (pageButton);

    keyHint.setText (gmosh::ComputerKeyboard::helpText(), juce::dontSendNotification);
    keyHint.setFont (juce::Font (juce::FontOptions (12.0f)));
    keyHint.setColour (juce::Label::textColourId, AccentGray);
    addAndMakeVisible (keyHint);

    newSeedButton.onClick = [this] { processorRef.newSeed(); };
    plantBranchButton.onClick = [this]
    {
        if (selectedBranch < 0)
            selectedBranch = processorRef.lastPlayedBranch();
        if (selectedBranch >= 0)
        {
            const juce::String id = AudioPluginAudioProcessor::branchParamId (selectedBranch);
            if (auto* p = param (processorRef, id))
                processorRef.plantBranch (selectedBranch, p->get());
        }
    };
    pageButton.onClick = [this] { showPage (! showingGenes); };

    std::array<juce::AudioParameterFloat*, gmosh::kNumBranches> branchParams {};
    for (int b = 0; b < gmosh::kNumBranches; ++b)
        branchParams[static_cast<std::size_t> (b)] = param (processorRef, AudioPluginAudioProcessor::branchParamId (b));
    plantView.setParameters (branchParams, param (processorRef, "rotation"));

    plantView.onPlayNote = [this] (int note, float velocity)
    {
        if (velocity > 0.0f)
        {
            selectedBranch = processorRef.lastPlayedBranch();
            processorRef.playNote (note, velocity);
        }
        else
        {
            processorRef.stopNote (note);
        }
    };

    plantView.onPlantBranch = [this] (int branch)
    {
        const juce::String id = AudioPluginAudioProcessor::branchParamId (branch);
        if (auto* p = param (processorRef, id))
            processorRef.plantBranch (branch, p->get());
    };

    geneGrid.getGeneValue = [this] (std::size_t gene)
    {
        return processorRef.seedGenome()[gene];
    };
    geneGrid.setGeneValue = [this] (std::size_t gene, float value)
    {
        processorRef.setSeedGene (gene, value);
    };

    addAndMakeVisible (plantView);
    geneGrid.setVisible (false);
    addAndMakeVisible (geneGrid);

    setSize (760, 620);
    startTimerHz (30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Nothing may be left sounding when the window goes away.
    computerKeys.releaseAll();
    plantView.releaseAllNotes();

    setLookAndFeel (nullptr);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::showPage (bool genes)
{
    showingGenes = genes;
    plantView.setVisible (! genes);
    geneGrid.setVisible (genes);
    pageButton.setButtonText (genes ? "plant" : "genes");
    pageLabel.setText (genes ? "gene table" : "seed / branches", juce::dontSendNotification);

    if (genes)
        geneGrid.refresh();

    resized();
}

//==============================================================================
// Computer keyboard.
bool AudioPluginAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    return computerKeys.handleKeyPress (key);
}

bool AudioPluginAudioProcessorEditor::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        computerKeys.handleKeyStateChanged();

    return false;   // never swallow the key, sliders and buttons still need it
}

void AudioPluginAudioProcessorEditor::updateKeyHint()
{
    if (computerKeys.octave() == shownOctave)
        return;

    shownOctave = computerKeys.octave();

    keyHint.setText (gmosh::ComputerKeyboard::helpText()
                       + (shownOctave == 0 ? juce::String()
                                           : "   octave " + juce::String (shownOctave)),
                     juce::dontSendNotification);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::timerCallback()
{
    // Focus went somewhere else (or the window is hidden): let go of anything
    // the computer keyboard is still holding.
    if (! hasKeyboardFocus (true))
        computerKeys.releaseAll();

    updateKeyHint();

    std::array<float, gmosh::kNumBranches> lengths {};

    for (int b = 0; b < gmosh::kNumBranches; ++b)
    {
        const juce::String id = AudioPluginAudioProcessor::branchParamId (b);
        if (auto* p = param (processorRef, id))
            lengths[static_cast<std::size_t> (b)] = p->get();
    }

    auto* rotationParam = param (processorRef, "rotation");

    plantView.update (processorRef.lastPlantSnapshot(),
                      lengths,
                      rotationParam != nullptr ? rotationParam->get() : 0.0f,
                      processorRef.lastPlayedBranch(),
                      processorRef.currentWheel());

    wheelLabel.setText ("wheel " + juce::String (processorRef.currentWheel(), 2),
                        juce::dontSendNotification);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    graphics.fillAll (BGGray);

    graphics.setColour (AccentGray);
    graphics.drawLine (0.0f, 44.0f, static_cast<float> (getWidth()), 44.0f, 2.0f);
    graphics.drawLine (0.0f, static_cast<float> (getHeight()) - 92.0f,
                       static_cast<float> (getWidth()), static_cast<float> (getHeight()) - 92.0f, 2.0f);
}

void AudioPluginAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (44).reduced (12, 8);
    title.setBounds (header.removeFromLeft (220));
    pageButton.setBounds (header.removeFromRight (90).reduced (0, 2));
    plantBranchButton.setBounds (header.removeFromRight (110).reduced (0, 2));
    newSeedButton.setBounds (header.removeFromRight (100).reduced (0, 2));
    pageLabel.setBounds (header);

    auto footer = area.removeFromBottom (92).reduced (16, 10);
    const int sliderWidth = footer.getWidth() / 3 - 12;

    for (int i = 0; i < 3; ++i)
    {
        juce::Slider* sliders[3] = { &wheelScaleSlider, &velSensSlider, &volumeSlider };
        sliders[i]->setBounds (footer.getX() + i * (sliderWidth + 18), footer.getY() + 18,
                               sliderWidth, 26);
    }

    keyHint.setBounds (footer.getX(), footer.getBottom() - 18, footer.getWidth(), 16);

    auto body = area.reduced (12, 8);

    if (showingGenes)
    {
        geneGrid.setBounds (body);
        plantView.setBounds (0, 0, 0, 0);
        return;
    }

    const int side = juce::jmin (body.getWidth() - 260, body.getHeight());
    plantView.setBounds (body.getX(), body.getY(), side, side);

    auto controls = body.withTrimmedLeft (side + 16);
    wheelLabel.setBounds (controls.removeFromTop (20));

    const int knobWidth = controls.getWidth() / 2;
    const int knobHeight = controls.getHeight() / 2;

    tuningKnob.setBounds (controls.getX(), controls.getY(), knobWidth, knobHeight);
    atonalityKnob.setBounds (controls.getX() + knobWidth, controls.getY(), knobWidth, knobHeight);
    effectKnob.setBounds (controls.getX(), controls.getY() + knobHeight, knobWidth, knobHeight);
    rotationKnob.setBounds (controls.getX() + knobWidth, controls.getY() + knobHeight,
                           knobWidth, knobHeight);
}
