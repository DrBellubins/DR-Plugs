#include "ComputerKeyboard.h"

namespace gmosh
{
//==============================================================================
// The Chronoverb map, expressed as rows instead of absolute notes so the
// octave shift is one integer.
const std::vector<ComputerKeyboard::Mapping>& ComputerKeyboard::mappings()
{
    static const std::vector<Mapping> map {
        // lower row: z s x c v b n m with s d g h j as the sharps
        { 'z', 0,  0 }, { 's', 0,  1 }, { 'x', 0,  2 }, { 'd', 0,  3 },
        { 'c', 0,  4 }, { 'v', 0,  5 }, { 'g', 0,  6 }, { 'b', 0,  7 },
        { 'h', 0,  8 }, { 'n', 0,  9 }, { 'j', 0, 10 }, { 'm', 0, 11 },

        // upper row: q w e r t y u with 1 2 5 6 7 as the sharps
        { 'q', 1,  0 }, { '1', 1,  1 }, { 'w', 1,  2 }, { '2', 1,  3 },
        { 'e', 1,  4 }, { 'r', 1,  5 }, { '5', 1,  6 }, { 't', 1,  7 },
        { '6', 1,  8 }, { 'y', 1,  9 }, { '7', 1, 10 }, { 'u', 1, 11 },
    };

    return map;
}

int ComputerKeyboard::normalise (int keyCode)
{
    // Letters arrive uppercase (JUCE key codes) or lowercase (text
    // characters); both mean the same key.
    if (keyCode >= 'A' && keyCode <= 'Z')
        return keyCode - 'A' + 'a';

    return keyCode;
}

const ComputerKeyboard::Mapping* ComputerKeyboard::find (int keyCode)
{
    for (const auto& m : mappings())
        if (m.key == keyCode)
            return &m;

    return nullptr;
}

int ComputerKeyboard::noteFor (const Mapping& mapping) const
{
    return 48 + 12 * (octaveShift + mapping.row) + mapping.semitones;
}

//==============================================================================
bool ComputerKeyboard::handleKeyPress (const juce::KeyPress& key)
{
    const int code = normalise (key.getKeyCode());

    if (code == ',')
    {
        setOctave (octaveShift - 1);
        return true;
    }

    if (code == '.')
    {
        setOctave (octaveShift + 1);
        return true;
    }

    const Mapping* mapping = find (code);
    if (mapping == nullptr)
        return false;

    // Auto repeat sends the same key press over and over: only the first one
    // is a note on.
    for (const auto& h : held)
        if (h.first == code)
            return true;

    noteOn (code, noteFor (*mapping));
    return true;
}

//==============================================================================
void ComputerKeyboard::handleKeyStateChanged()
{
    for (std::size_t i = held.size(); i > 0; --i)
    {
        const std::size_t index = i - 1;
        const int code = held[index].first;

        if (juce::KeyPress::isKeyCurrentlyDown (code))
            continue;

        // Some back ends report letter keys uppercase only.
        if (code >= 'a' && code <= 'z'
         && juce::KeyPress::isKeyCurrentlyDown (code - 'a' + 'A'))
            continue;

        noteOffAt (index);
    }
}

//==============================================================================
void ComputerKeyboard::noteOn (int keyCode, int midiNote)
{
    held.emplace_back (keyCode, midiNote);

    if (onNote != nullptr)
        onNote (midiNote, 0.80f);
}

void ComputerKeyboard::noteOffAt (std::size_t index)
{
    const int midiNote = held[index].second;
    held.erase (held.begin() + static_cast<std::ptrdiff_t> (index));

    if (onNote != nullptr)
        onNote (midiNote, 0.0f);   // velocity 0 means note off
}

void ComputerKeyboard::releaseAll()
{
    while (! held.empty())
        noteOffAt (held.size() - 1);
}

void ComputerKeyboard::setOctave (int newOctave)
{
    const int clamped = juce::jlimit (kMinOctave, kMaxOctave, newOctave);
    if (clamped == octaveShift)
        return;

    releaseAll();   // a held note would otherwise keep the old pitch
    octaveShift = clamped;
}

//==============================================================================
juce::String ComputerKeyboard::helpText()
{
    return "keys  z s x c v b n m   q 2 w e 5 r t 6 y u   , .  octave";
}

} // namespace gmosh
