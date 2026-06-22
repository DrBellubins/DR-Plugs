#include "ComputerKeyboardSquareSynth.h"

// ============================== Construction ==============================

ComputerKeyboardSquareSynth::ComputerKeyboardSquareSynth()
{
    Voices.resize(MaxVoices);
    BuildDefaultKeyMap();
}

ComputerKeyboardSquareSynth::~ComputerKeyboardSquareSynth()
{
}

// ============================== Public API ==============================

void ComputerKeyboardSquareSynth::PrepareToPlay(double NewSampleRate)
{
    SampleRate = (NewSampleRate > 0.0 ? NewSampleRate : 44100.0);

    for (Voice& VoiceRef : Voices)
    {
        VoiceRef.IsActive = false;
        VoiceRef.MidiNote = -1;
        VoiceRef.Phase = 0.0;
        VoiceRef.PhaseIncrement = 0.0;
        VoiceRef.Amplitude = 0.0f;
        VoiceRef.TargetAmplitude = 0.0f;
    }

    HeldKeys.clear();

    EventReadIndex.store(0, std::memory_order_relaxed);
    EventWriteIndex.store(0, std::memory_order_relaxed);
    for (auto& Evt : EventQueue)
        Evt = {};
}

void ComputerKeyboardSquareSynth::Process(juce::AudioBuffer<float>& AudioBuffer)
{
    DrainNoteEvents();

    const int NumChannels = AudioBuffer.getNumChannels();
    const int NumSamples = AudioBuffer.getNumSamples();

    if (NumChannels <= 0 || NumSamples <= 0)
        return;

    for (int SampleIndex = 0; SampleIndex < NumSamples; ++SampleIndex)
    {
        float MixedSample = 0.0f;

        for (Voice& VoiceRef : Voices)
        {
            // Slew amplitude towards target
            float Delta = VoiceRef.TargetAmplitude - VoiceRef.Amplitude;
            VoiceRef.Amplitude += AmplitudeSlew * Delta;

            // Deactivate silent voices that are fully released
            if (!VoiceRef.IsActive && std::abs(VoiceRef.Amplitude) < 1.0e-5f && VoiceRef.TargetAmplitude <= 0.0f)
            {
                VoiceRef.MidiNote = -1;
                VoiceRef.Amplitude = 0.0f;
                VoiceRef.Phase = 0.0;
                VoiceRef.PhaseIncrement = 0.0;

                continue;
            }

            if (VoiceRef.IsActive || VoiceRef.Amplitude > 1.0e-6f)
            {
                // Square wave
                float Oscillator = (VoiceRef.Phase < 0.5 ? 1.0f : -1.0f);

                // Apply amplitude and master gain
                MixedSample += (Oscillator * VoiceRef.Amplitude * OutputGain);

                // Advance phase
                VoiceRef.Phase += VoiceRef.PhaseIncrement;

                if (VoiceRef.Phase >= 1.0)
                    VoiceRef.Phase -= 1.0;
            }
        }

        // Write the mixed sample to all channels (add in place)
        for (int ChannelIndex = 0; ChannelIndex < NumChannels; ++ChannelIndex)
        {
            float* ChannelData = AudioBuffer.getWritePointer(ChannelIndex);
            ChannelData[SampleIndex] += MixedSample;
        }
    }
}

void ComputerKeyboardSquareSynth::HandleKeyChange(int KeyCode, bool IsKeyDown)
{
    // Normalize letter case (support both upper and lower)
    if (KeyCode >= 'A' && KeyCode <= 'Z')
        KeyCode = static_cast<int>(KeyCode - 'A' + 'a');

    auto MapIt = KeyToMidi.find(KeyCode);

    if (MapIt == KeyToMidi.end())
        return;

    const int MidiNote = MapIt->second;

    // Debounce on the message thread so we don't enqueue repeats
    if (IsKeyDown)
    {
        if (!HeldKeys.insert(KeyCode).second)
            return;
    }
    else
    {
        if (HeldKeys.erase(KeyCode) == 0)
            return;
    }

    const int write = EventWriteIndex.load(std::memory_order_relaxed);
    const int read  = EventReadIndex.load(std::memory_order_acquire);
    const int nextWrite = (write + 1) % EventQueueSize;

    // Queue full: drop the event rather than blocking the message thread
    if (nextWrite == read)
        return;

    EventQueue[write] = { MidiNote, IsKeyDown };
    EventWriteIndex.store(nextWrite, std::memory_order_release);
}

void ComputerKeyboardSquareSynth::SetOutputGain(float NewGain)
{
    OutputGain = juce::jlimit(0.0f, 1.0f, NewGain);
}

std::vector<int> ComputerKeyboardSquareSynth::GetMappedKeyCodes() const
{
    std::vector<int> Keys;
    Keys.reserve(KeyToMidi.size());

    for (const auto& Pair : KeyToMidi)
        Keys.push_back(Pair.first);

    return Keys;
}

// ============================== Internals ==============================

// At the TOP of Process(), before the sample loop (audio thread):
void ComputerKeyboardSquareSynth::DrainNoteEvents()
{
    int read = EventReadIndex.load(std::memory_order_relaxed);
    const int write = EventWriteIndex.load(std::memory_order_acquire);

    while (read != write)
    {
        const NoteEvent evt = EventQueue[read];

        if (evt.IsOn)
            NoteOn(evt.MidiNote);
        else
            NoteOff(evt.MidiNote);

        read = (read + 1) % EventQueueSize;
    }

    EventReadIndex.store(read, std::memory_order_release);
}

double ComputerKeyboardSquareSynth::MidiNoteToFrequency(int MidiNote)
{
    return 440.0 * std::pow(2.0, (static_cast<double>(MidiNote) - 69.0) / 12.0);
}

void ComputerKeyboardSquareSynth::NoteOn(int MidiNote)
{
    int VoiceIndex = FindExistingVoiceForNote(MidiNote);

    if (VoiceIndex < 0)
    {
        VoiceIndex = FindFreeVoiceIndex();

        if (VoiceIndex < 0)
        {
            VoiceIndex = StealVoiceIndex();
        }
    }

    Voice& VoiceRef = Voices[VoiceIndex];
    VoiceRef.IsActive = true;
    VoiceRef.MidiNote = MidiNote;
    VoiceRef.TargetAmplitude = 1.0f; // Raw voice level; overall output scaled by OutputGain

    const double Frequency = MidiNoteToFrequency(MidiNote);
    VoiceRef.PhaseIncrement = (Frequency / std::max(1.0, SampleRate));
    // Optional: reset phase to start waves in-phase
    VoiceRef.Phase = 0.0;
}

void ComputerKeyboardSquareSynth::NoteOff(int MidiNote)
{
    int VoiceIndex = FindExistingVoiceForNote(MidiNote);

    if (VoiceIndex >= 0)
    {
        Voice& VoiceRef = Voices[VoiceIndex];
        VoiceRef.IsActive = false;
        VoiceRef.TargetAmplitude = 0.0f; // Release (slew will ramp down)
    }
}

int ComputerKeyboardSquareSynth::FindExistingVoiceForNote(int MidiNote) const
{
    for (int Index = 0; Index < static_cast<int>(Voices.size()); ++Index)
    {
        if (Voices[static_cast<size_t>(Index)].MidiNote == MidiNote)
            return Index;
    }

    return -1;
}

int ComputerKeyboardSquareSynth::FindFreeVoiceIndex() const
{
    for (int Index = 0; Index < static_cast<int>(Voices.size()); ++Index)
    {
        const Voice& VoiceRef = Voices[static_cast<size_t>(Index)];

        if (!VoiceRef.IsActive && VoiceRef.TargetAmplitude <= 0.0f && std::abs(VoiceRef.Amplitude) < 1.0e-5f)
            return Index;
    }

    return -1;
}

int ComputerKeyboardSquareSynth::StealVoiceIndex() const
{
    // Steal the quietest voice
    int BestIndex = 0;
    float BestLevel = std::numeric_limits<float>::max();

    for (int Index = 0; Index < static_cast<int>(Voices.size()); ++Index)
    {
        float Level = std::abs(Voices[static_cast<size_t>(Index)].Amplitude);

        if (Level < BestLevel)
        {
            BestLevel = Level;
            BestIndex = Index;
        }
    }

    return BestIndex;
}

void ComputerKeyboardSquareSynth::BuildDefaultKeyMap()
{
    KeyToMidi.clear();

    // Lower row (like a piano 'Z' row): starting at C3 (MIDI 48)
    // White keys: Z X C V B N M
    KeyToMidi['z'] = 48; // C3
    KeyToMidi['x'] = 50; // D3
    KeyToMidi['c'] = 52; // E3
    KeyToMidi['v'] = 53; // F3
    KeyToMidi['b'] = 55; // G3
    KeyToMidi['n'] = 57; // A3
    KeyToMidi['m'] = 59; // B3

    // Black keys between them using nearby letters (S D G H J)
    KeyToMidi['s'] = 49; // C#3/Db3
    KeyToMidi['d'] = 51; // D#3/Eb3
    KeyToMidi['g'] = 54; // F#3/Gb3
    KeyToMidi['h'] = 56; // G#3/Ab3
    KeyToMidi['j'] = 58; // A#3/Bb3

    // Upper row (like a piano 'Q' row): starting at C4 (MIDI 60)
    // White keys: Q W E R T Y U
    KeyToMidi['q'] = 60; // C4
    KeyToMidi['w'] = 62; // D4
    KeyToMidi['e'] = 64; // E4
    KeyToMidi['r'] = 65; // F4
    KeyToMidi['t'] = 67; // G4
    KeyToMidi['y'] = 69; // A4
    KeyToMidi['u'] = 71; // B4

    // Black keys between them using nearby letters (2 3 5 6 7) are typical,
    // but we stay with letters only per request, so use adjacent letters:
    KeyToMidi['1'] = 61; // Optional (if allowed)
    KeyToMidi['2'] = 63; // Optional (if allowed)
    KeyToMidi['5'] = 66; // Optional (if allowed)
    KeyToMidi['6'] = 68; // Optional (if allowed)
    KeyToMidi['7'] = 70; // Optional (if allowed)

    // Also accept uppercase versions transparently via HandleKeyChange conversion.
}