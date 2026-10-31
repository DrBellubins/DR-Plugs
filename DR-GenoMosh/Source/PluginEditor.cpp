#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Utils/Theme.h"

//==============================================================================
namespace
{
juce::AudioParameterFloat* param(AudioPluginAudioProcessor& processor, const juce::String& id)
{
    return dynamic_cast<juce::AudioParameterFloat*>(processor.parameters.getParameter(id));
}
} // namespace

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor& ownerProcessor)
    : AudioProcessorEditor(&ownerProcessor), processorRef(ownerProcessor)
{
    setLookAndFeel(&lookAndFeel);

    // The editor itself takes key focus so the computer keyboard plays the
    // synth even when nothing else is focused (standalone testing, no DAW).
    setWantsKeyboardFocus(true);

    computerKeys.onNote = [this](int note, float velocity)
    {
        if (velocity > 0.0f)
            processorRef.playNote(note, velocity);
        else
            processorRef.stopNote(note);
    };

    auto attach = [this](const juce::String& id, juce::Slider& slider)
    {
        if (auto* p = param(processorRef, id))
        {
            const auto& range = p->getNormalisableRange();
            slider.setRange(static_cast<double>(range.start), static_cast<double>(range.end));
            slider.setValue(static_cast<double>(p->get()), juce::dontSendNotification);

            attachments.push_back(std::make_unique<juce::SliderParameterAttachment>(*p, slider, nullptr));
        }
    };

    attach("volume", volumeSlider);
    attach("velSens", velSensSlider);
    attach("wheelScale", wheelScaleSlider);
    attach("tuning", tuningKnob);
    attach("atonality", atonalityKnob);
    attach("effect", effectKnob);

    for (juce::Slider* s : { &volumeSlider, &velSensSlider, &wheelScaleSlider })
    {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 48, 18);

        addAndMakeVisible(s);
    }

    for (juce::Slider* s : { &tuningKnob, &atonalityKnob, &effectKnob })
        addAndMakeVisible(s);

    //------------------------------------------------------------------------
    // What the live controls are for. Everything on a dark plate is a
    // performance macro; everything on the plain background is seed generation.
    struct SliderCaption
    {
        juce::Label* label;
        juce::Slider* slider;
        const char* text;
        const char* tip;
    };

    const SliderCaption captions[3] = {
        { &wheelScaleLabel, &wheelScaleSlider, "wheel scale",
          "How far the mod wheel (CC1) opens the plant. At 0 every branch plays the seed; at 1 the branch "
          "lengths are used in full." },
        { &velSensLabel, &velSensSlider, "velocity",
          "How much playing harder changes loudness and the modulation envelope." },
        { &volumeLabel, &volumeSlider, "volume", "Master output level." },
    };
    for (const auto& caption : captions)
    {
        caption.label->setText(caption.text, juce::dontSendNotification);
        caption.label->setFont(juce::Font(juce::FontOptions(12.0f)));
        caption.label->setColour(juce::Label::textColourId, CaptionGray);
        caption.label->setTooltip(caption.tip);
        caption.slider->setTooltip(caption.tip);
        addAndMakeVisible(caption.label);
    }

    tuningKnob.setTooltip("Master tune in cents.");

    atonalityKnob.setTooltip(
        "How much the plant is allowed to affect tonality.\n"
        "0: oscillator offsets are unison and there is no vibrato, drift or detune.\n"
        "1: the full range of those genes is available.\n"
        "This is a generation control: it rebuilds the plant, it never retunes a note you are holding.");

    effectKnob.setTooltip("Scales the chorus / reverb wet path of whatever is playing.");

    liveCaption.setText("live macros", juce::dontSendNotification);
    outputCaption.setText("output / response", juce::dontSendNotification);

    for (juce::Label* label : { &liveCaption, &outputCaption })
    {
        label->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        label->setColour(juce::Label::textColourId, CaptionGray);
        addAndMakeVisible(label);
    }

    // Tooltips belong to the editor in a plug-in, so they scale with the DAW.
    tooltips = std::make_unique<juce::TooltipWindow>(this, 600);

    title.setText("GENOMOSH", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(21.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, juce::Colours::lightgoldenrodyellow);
    addAndMakeVisible(title);

    pageLabel.setText("seed / branches", juce::dontSendNotification);
    addAndMakeVisible(pageLabel);

    wheelLabel.setText("wheel", juce::dontSendNotification);
    addAndMakeVisible(wheelLabel);

    addAndMakeVisible(newSeedButton);
    addAndMakeVisible(plantBranchButton);
    addAndMakeVisible(pageButton);

    keyHint.setText(gmosh::ComputerKeyboard::helpText(), juce::dontSendNotification);
    keyHint.setFont(juce::Font(juce::FontOptions(12.0f)));
    keyHint.setColour(juce::Label::textColourId, CaptionGray);

    addAndMakeVisible(keyHint);

    newSeedButton.onClick = [this]
    {
        processorRef.newSeed(); // also folds the branch lengths back to default
        selectedBranch = -1;
        geneGrid.refresh();
    };

    plantBranchButton.onClick = [this]
    {
        if (selectedBranch < 0)
            selectedBranch = processorRef.lastPlayedBranch();

        if (selectedBranch >= 0)
        {
            const juce::String id = AudioPluginAudioProcessor::branchParamId(selectedBranch);

            if (auto* parameter = param(processorRef, id))
                processorRef.plantBranch(selectedBranch, parameter->get());
        }
    };
    pageButton.onClick = [this] { showPage(!showingGenes); };

    std::array<juce::AudioParameterFloat*, gmosh::kNumBranches> branchParams{};

    for (int b = 0; b < gmosh::kNumBranches; ++b)
        branchParams[static_cast<std::size_t>(b)] =
            param(processorRef, AudioPluginAudioProcessor::branchParamId(b));

    plantView.setParameters(branchParams);

    plantView.onPlayNote = [this](int note, float velocity)
    {
        if (velocity > 0.0f)
        {
            selectedBranch = processorRef.lastPlayedBranch();
            processorRef.playNote(note, velocity);
        }
        else
        {
            processorRef.stopNote(note);
        }
    };

    plantView.onPlantBranch = [this](int branch)
    {
        const juce::String id = AudioPluginAudioProcessor::branchParamId(branch);

        if (auto* parameter = param(processorRef, id))
            processorRef.plantBranch(branch, parameter->get());
    };

    geneGrid.getGeneValue = [this](std::size_t gene) { return processorRef.seedGenome()[gene]; };
    geneGrid.setGeneValue = [this](std::size_t gene, float value) { processorRef.setSeedGene(gene, value); };

    addAndMakeVisible(plantView);
    geneGrid.setVisible(false);
    addAndMakeVisible(geneGrid);

    setSize(760, 620);
    startTimerHz(30);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    // Nothing may be left sounding when the window goes away.
    computerKeys.releaseAll();
    plantView.releaseAllNotes();

    setLookAndFeel(nullptr);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::showPage(bool genes)
{
    showingGenes = genes;
    plantView.setVisible(!genes);
    geneGrid.setVisible(genes);
    pageButton.setButtonText(genes ? "plant" : "genes");
    pageLabel.setText(genes ? "gene table" : "seed / branches", juce::dontSendNotification);

    // The macro plate belongs to the plant page; the gene page uses the whole
    // body area.
    for (juce::Slider* s : { &tuningKnob, &atonalityKnob, &effectKnob })
        s->setVisible(!genes);

    wheelLabel.setVisible(!genes);
    liveCaption.setVisible(!genes);

    if (genes)
        geneGrid.refresh();

    resized();
}

//==============================================================================
// Computer keyboard.
bool AudioPluginAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    return computerKeys.handleKeyPress(key);
}

bool AudioPluginAudioProcessorEditor::keyStateChanged(bool isKeyDown)
{
    if (!isKeyDown)
        computerKeys.handleKeyStateChanged();

    return false; // never swallow the key, sliders and buttons still need it
}

void AudioPluginAudioProcessorEditor::updateKeyHint()
{
    if (computerKeys.octave() == shownOctave)
        return;

    shownOctave = computerKeys.octave();

    keyHint.setText(gmosh::ComputerKeyboard::helpText()
                        + (shownOctave == 0 ? juce::String() : "   octave " + juce::String(shownOctave)),
                    juce::dontSendNotification);
}

//==============================================================================
void AudioPluginAudioProcessorEditor::timerCallback()
{
    // Focus went somewhere else (or the window is hidden): let go of anything
    // the computer keyboard is still holding.
    if (!hasKeyboardFocus(true))
        computerKeys.releaseAll();

    updateKeyHint();

    std::array<float, gmosh::kNumBranches> lengths{};

    for (int branch = 0; branch < gmosh::kNumBranches; ++branch)
    {
        const juce::String id = AudioPluginAudioProcessor::branchParamId(branch);

        if (auto* parameter = param(processorRef, id))
            lengths[static_cast<std::size_t>(branch)] = parameter->get();
    }

    plantView.update(processorRef.lastPlantSnapshot(), lengths, processorRef.lastPlayedBranch(),
                     processorRef.currentWheel());

    // The wheel readout also shows what the wheel scale slider is doing, which
    // is otherwise invisible: it is a multiplier on how far the plant opens.
    const double wheelNow  = processorRef.currentWheel();
    const double wheelOpen = wheelScaleSlider.getValue();

    wheelLabel.setText("wheel " + juce::String(wheelNow, 2) + "  x " + juce::String(wheelOpen, 2) + "  = "
                           + juce::String(wheelNow * wheelOpen, 2) + " open",
                       juce::dontSendNotification);
}

//==============================================================================
// One geometry, used by both paint() and resized(): the dark plates drawn behind
// the live controls have to land exactly where the widgets go.
AudioPluginAudioProcessorEditor::Layout AudioPluginAudioProcessorEditor::layout() const
{
    auto area = getLocalBounds();

    Layout layout;
    layout.header = area.removeFromTop(kHeaderHeight).reduced(12, 8);
    const auto footerBand = area.removeFromBottom(kFooterHeight);

    layout.body = area.reduced(10, 6);

    const int side  = juce::jmin(layout.body.getWidth() - kSideColumnWidth, layout.body.getHeight());
    layout.plant = { layout.body.getX(), layout.body.getY(), side, side };
    layout.controlsPanel = layout.body.withTrimmedLeft(side + 12);

    // Top row: generation facing, on the plain background. Bottom row: the live
    // macros, which get the darker plate. The left half of the top row is left
    // empty on purpose.
    auto controls = layout.controlsPanel.reduced(12, 10);
    auto rows = controls.withTrimmedTop(44); // wheel readout lives above

    const int rowHeight = rows.getHeight() / 2;
    layout.knobRowTop = { rows.getX(), rows.getY(), rows.getWidth(), rowHeight };
    layout.knobRowBottom = { rows.getX(), rows.getBottom() - rowHeight, rows.getWidth(), rowHeight };
    layout.macroPlate = layout.knobRowBottom.expanded(8);

    layout.footerPanel = footerBand.reduced(8, 6);
    layout.footer = footerBand.reduced(20, 12);

    return layout;
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    const auto mLayout = layout();

    graphics.fillAll(BGGray);

    graphics.setColour(AccentGray);
    graphics.drawLine(0.0f, static_cast<float>(kHeaderHeight), static_cast<float>(getWidth()),
                      static_cast<float>(kHeaderHeight), 2.0f);

    // Seed generation lives on the plain background; only the controls that can
    // be moved while the synth is sounding sit on a darker plate.
    auto drawPlate = [&graphics](juce::Rectangle<int> area)
    {
        const auto rect = area.toFloat().reduced(1.0f);

        graphics.setColour(PanelGray);
        graphics.fillRoundedRectangle(rect, 10.0f);
        graphics.setColour(PanelEdge);
        graphics.drawRoundedRectangle(rect, 10.0f, 1.0f);
    };

    drawPlate(mLayout.footerPanel);

    if (!showingGenes)
        drawPlate(mLayout.macroPlate);
}

void AudioPluginAudioProcessorEditor::resized()
{
    const auto mLayout = layout();

    auto header = mLayout.header;
    title.setBounds(header.removeFromLeft(220));
    pageButton.setBounds(header.removeFromRight(90).reduced(0, 2));
    plantBranchButton.setBounds(header.removeFromRight(110).reduced(0, 2));
    newSeedButton.setBounds(header.removeFromRight(100).reduced(0, 2));
    pageLabel.setBounds(header);

    //------------------------------------------------------------------------
    // Output plate: caption, then three labelled sliders, then the key hint.
    outputCaption.setBounds(mLayout.footer.getX(), mLayout.footer.getY(), 300, 14);

    const int rowTop = mLayout.footer.getY() + 20;
    const int labelHeight = 14, sliderHeight = 26;
    const int sliderWidth = mLayout.footer.getWidth() / 3 - 12;

    juce::Slider* sliders[3] = { &wheelScaleSlider, &velSensSlider, &volumeSlider };
    juce::Label* labels[3]   = { &wheelScaleLabel, &velSensLabel, &volumeLabel };

    for (int i = 0; i < 3; ++i)
    {
        const int x = mLayout.footer.getX() + i * (sliderWidth + 18);
        labels[i]->setBounds(x, rowTop, 140, labelHeight);
        sliders[i]->setBounds(x, rowTop + labelHeight + 2, sliderWidth, sliderHeight);
    }

    keyHint.setBounds(mLayout.footer.getX(), mLayout.footer.getBottom() - 18, mLayout.footer.getWidth(), 16);

    //------------------------------------------------------------------------
    // Generation facing row: no plate, it is not something you move while a
    // note is sounding.
    wheelLabel.setBounds(mLayout.controlsPanel.getX() + 12, mLayout.controlsPanel.getY() + 12,
                         mLayout.controlsPanel.getWidth() - 24, 18);

    const int topKnobWidth = mLayout.knobRowTop.getWidth() / 2;
    const auto topRow = mLayout.knobRowTop.reduced(0, 12); // keep its text box clear of the plate
    atonalityKnob.setBounds(topRow.getX() + topKnobWidth, topRow.getY(), topKnobWidth, topRow.getHeight());

    //------------------------------------------------------------------------
    // Live macro plate: tuning and effect, the two things that are safe to ride
    // while playing.
    liveCaption.setBounds(mLayout.macroPlate.getX() + 12, mLayout.macroPlate.getY() + 6, 200, 14);

    const auto bottomRow = mLayout.knobRowBottom.reduced(6, 20); // room for the caption
    const int knobWidth = bottomRow.getWidth() / 2;
    const int knobHeight = bottomRow.getHeight();

    effectKnob.setBounds(bottomRow.getX(), bottomRow.getY(), knobWidth, knobHeight);
    tuningKnob.setBounds(bottomRow.getX() + knobWidth, bottomRow.getY(), knobWidth, knobHeight);

    //------------------------------------------------------------------------
    // Seed / plant area.
    if (showingGenes)
    {
        geneGrid.setBounds(mLayout.body);
        plantView.setBounds({});

        return;
    }

    geneGrid.setBounds({});
    plantView.setBounds(mLayout.plant);
}
