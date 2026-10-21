#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <utility>
#include <vector>

namespace gmosh
//==============================================================================
// Computer keyboard playing, ported from the Chronoverb
// ComputerKeyboardSquareSynth key map: two piano like rows of letters.
//
//   z s x c v b n m        C3 .. B3   (sharps on s d g h j)
//   q 2 w e 5 r t 6 y u    C4 .. B4   (sharps on 2 5 6 7)
//
// comma and period transpose an octave. Notes leave through onNote with a
// velocity for note on and 0 for note off, which is the same convention the
// plant view uses, so the editor wires both to the processor the same way.
//
// Releases come from keyStateChanged + KeyPress::isKeyCurrentlyDown, the only
// reliable way to learn about a key up in JUCE; unlike the original we poll
// just the keys we are holding rather than the whole map.
//==============================================================================
{
class ComputerKeyboard
{
public:
    std::function<void (int midiNote, float velocity)> onNote;

    /// Returns true when the key belongs to this keyboard (consumed).
    bool handleKeyPress (const juce::KeyPress&);

    /// From keyStateChanged: releases the held keys that are no longer down.
    void handleKeyStateChanged();

    /// Note off for everything still held (focus left the editor, closing...).
    void releaseAll();

    int octave() const { return octaveShift; }
    void setOctave (int newOctave);

    /// One line of help for the UI.
    static juce::String helpText();

private:
    struct Mapping
    {
        int key;         ///< normalised key code
        int row;         ///< 0 = lower row (C3), 1 = upper row (C4)
        int semitones;   ///< from the root of that row
    };

    static const std::vector<Mapping>& mappings();
    static int normalise (int keyCode);
    static const Mapping* find (int keyCode);

    int noteFor (const Mapping& mapping) const;
    void noteOn (int keyCode, int midiNote);
    void noteOffAt (std::size_t index);

    int octaveShift = 0;
    std::vector<std::pair<int, int>> held;   ///< key code -> midi note

    static constexpr int kMinOctave = -3;
    static constexpr int kMaxOctave = 3;
};

} // namespace gmosh
