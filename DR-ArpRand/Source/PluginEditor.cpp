#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Utils/FlatRotaryLookAndFeel.h"
#include "Utils/Theme.h"
#include "Utils/ThemedCheckbox.h"

static FlatRotaryLookAndFeel flatKnobLAF;

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (AudioPluginAudioProcessor& processor)
    : AudioProcessorEditor (&processor), processorRef (processor)
{
    juce::ignoreUnused (processorRef);

    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (700, 300);

    startTimerHz(20); // Update 20 times per second

    // Rate Knob
    arpRateKnob = std::make_unique<ThemedKnob>(
        "Arp Rate", nullptr, nullptr, " Rate", juce::Slider::NoTextBox);

    arpRateKnob->setTextValueSuffix(" Rate");
    arpRateKnob->setLookAndFeel(&flatKnobLAF);

    arpRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.parameters, "arpRate", *arpRateKnob);

    addAndMakeVisible(*arpRateKnob);

    arpRateKnob->setBounds((getWidth() / 2) - 100, (getHeight() / 2) - 100, 200, 200);

    // Free rate checkbox
    freeRateCheckbox = std::make_unique<ThemedCheckbox>("Free rate");

    freeRateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.parameters, "isFreeRate", *freeRateCheckbox);

    addAndMakeVisible(*freeRateCheckbox);

    freeRateCheckbox->setBounds(50, 50, 150, 32); // x, y, width, height
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
}

void AudioPluginAudioProcessorEditor::updateArpRateLabel(bool isFree)
{
    if (isFree)
    {
    	float arpRate = processorRef.parameters.getRawParameterValue("arpRate")->load();
    	juce::String label = juce::String("Arp Rate\n\n\n") + juce::String(arpRate) + juce::String(" Hz");

    	arpRateKnob->setLabelText(label);
    }
    else
    {
    	int arpRateIndex = static_cast<int>(processorRef.parameters.getRawParameterValue("arpRate")->load());
    	static const juce::StringArray BeatFractions { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    	juce::String label = juce::String("Arp Rate\n\n\n") + BeatFractions[arpRateIndex];

    	arpRateKnob->setLabelText(label);
    }
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& graphics)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    graphics.fillAll(BGGray);
}

void AudioPluginAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor.
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    bool IsFreeRate = processorRef.parameters.getRawParameterValue("freeRate")->load() > 0.5f;

    if (IsFreeRate)
    {
        arpRateKnob->setRange(0.5, 24.0, 0.01); // Example Hz range

        updateArpRateLabel(IsFreeRate);

        // Optionally, update valueToTextFunction and textToValueFunction for Hz
        arpRateKnob->setValueToTextFunction(nullptr); // Default function
        arpRateKnob->setTextToValueFunction(nullptr);
    }
    else
    {
        arpRateKnob->setRange(0, 5, 1); // Indices for beat fractions

        updateArpRateLabel(IsFreeRate); // Updates label text to fraction

        arpRateKnob->setValueToTextFunction([this](double Value)
        {
            static const juce::StringArray BeatFractions { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
            int Index = static_cast<int>(Value);
            return BeatFractions[Index];
        });

        arpRateKnob->setTextToValueFunction([this](const juce::String& Text)
        {
            static const juce::StringArray BeatFractions { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
            return (double)BeatFractions.indexOf(Text.trim());
        });
    }
}
