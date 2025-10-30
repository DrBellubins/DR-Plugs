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
	bool isFreeRate = processorRef.parameters.getRawParameterValue("isFreeRate")->load() > 0.5f;
	float arpRate = processorRef.parameters.getRawParameterValue("arpRate")->load();

	if (isFreeRate)
	{
		arpRateKnob->setRange(0.0, 1.0, 0.001); // Smooth

		// Customize for Hz mode
		float minHzMult = 0.03125f;
		float maxHzMult = 1.0f;
		float hzValue = arpRate * (maxHzMult - minHzMult) + minHzMult;

		arpRateKnob->setLabelText("Arp Rate\n\n\n" + juce::String(hzValue, 2) + " Hz");

		arpRateKnob->setValueToTextFunction(nullptr);
		arpRateKnob->setTextToValueFunction(nullptr);
	}
	else
	{
		arpRateKnob->setRange(0.0, 1.0, 1.0 / 5.0); // 6 steps

		// Quantize value to nearest step
		int arpRateIndex = static_cast<int>(std::round(arpRate * 5.0f));
		arpRateIndex = juce::jlimit(0, 5, arpRateIndex);
		static const juce::StringArray BeatFractions { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32" };
		arpRateKnob->setLabelText("Arp Rate\n\n\n" + BeatFractions[arpRateIndex]);

		arpRateKnob->setValueToTextFunction([=](double Value)
		{
			int Index = static_cast<int>(std::round(Value * 5.0));
			return BeatFractions[Index];
		});

		arpRateKnob->setTextToValueFunction([=](const juce::String& Text)
		{
			int Index = BeatFractions.indexOf(Text.trim());
			return (double)Index / 5.0;
		});
	}
}

