#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>

// ComputerKeyboardSquareSynth
// - Simple polyphonic square-wave generator driven by computer keyboard keys.
// - Intended for standalone plugin testing without a DAW.
// - Keys are mapped to musical notes (two rows for a piano-like layout).
// - Call PrepareToPlay() in your processor, Process() each block, and
//   forward key changes from your editor via HandleKeyChange().
//
// Usage notes:
// - Ensure your editor component has focus (setWantsKeyboardFocus(true))
//   and forwards key presses/releases to this synth.
// - The synth adds its output to the provided audio buffer (in-place).
// - Gain is conservative to avoid clipping; the host/plugin can additionally clamp.
class ComputerKeyboardSquareSynth
{
public:
    ComputerKeyboardSquareSynth();
    ~ComputerKeyboardSquareSynth();

    void PrepareToPlay(double NewSampleRate);

    // Add generated tone into the audio buffer (in-place add).
    void Process(juce::AudioBuffer<float>& AudioBuffer);

    // Handle a key change event. Pass ASCII code or KeyPress::getTextCharacter() where possible.
    // Set IsKeyDown = true on press, false on release.
    void HandleKeyChange(int KeyCode, bool IsKeyDown);

    // Adjust master gain of the synth output [0..1].
    void SetOutputGain(float NewGain);

    // Return the list of key codes this synth responds to (for polling in keyStateChanged).
    std::vector<int> GetMappedKeyCodes() const;

private:
    struct Voice
    {
        bool IsActive = false;
        int MidiNote = -1;
        double Phase = 0.0;
        double PhaseIncrement = 0.0;
        float Amplitude = 0.0f;
        float TargetAmplitude = 0.0f;
    };

    double SampleRate = 44100.0;
    float OutputGain = 0.20f;            // Conservative to avoid hard clipping on chords
    float AmplitudeSlew = 0.0040f;       // Per-sample amplitude slew for click reduction
    static constexpr int MaxVoices = 16; // Simple polyphony limit

    std::vector<Voice> Voices;

    // Map from key code to MIDI note
    std::unordered_map<int, int> KeyToMidi;

    // Track which keys are currently held (for debouncing)
    std::unordered_set<int> HeldKeys;

    // Helpers
    static double MidiNoteToFrequency(int MidiNote);
    void NoteOn(int MidiNote);
    void NoteOff(int MidiNote);
    int FindExistingVoiceForNote(int MidiNote) const;
    int FindFreeVoiceIndex() const;
    int StealVoiceIndex() const;

    void BuildDefaultKeyMap();
};