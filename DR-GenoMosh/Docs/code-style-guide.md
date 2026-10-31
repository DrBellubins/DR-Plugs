# C++ / JUCE Code Style

Follow this style for all code in this project. It deliberately differs from the
default JUCE template style (see "Things to avoid"). When editing an existing
file, match the surrounding code, but never copy legacy JUCE-style spacing or
abbreviated names from it. New and modified lines follow this guide.

## Naming

- **Never abbreviate names.** Spell every word out: `parameter`, not `p` or
  `param`; `branchIndex`, not `b` or `idx`; `geneIndex`, not `i`; `lock`, not
  `sl`; `randomGenerator`, not `rng`; `channelIndex`, not `ch`; `sampleCount`,
  not `numSamps`. This applies to loop counters, lambda parameters, locals,
  members, functions, and types.
- Exceptions: identifiers that come from JUCE, the standard library, or
  already-existing project code keep their spelling when you call or override
  them (`getNumSamples`, `kNumBranches`, `std::size_t`). Established acronyms
  (MIDI, DSP, UI, CC, FX, LFO) are fine. Do not invent new abbreviations.
- **Publicly visible things are `CamelCase`** (first letter uppercase): namespaces,
  classes, structs, enums and their values, type aliases, template parameters,
  public and protected methods and data members, and free functions declared in
  headers. Example: `PlantBuilder`, `BuildPlant()`, `Volume`.
- **Private things are `camelCase`** (first letter lowercase): private methods
  and data members, local variables, parameters, and functions that are only
  visible inside one `.cpp` file (anonymous namespace or `static`).
  Example: `seedIdentifier`, `rebuildPlant()`, `branchIndex`.
- Constants and constexpr values keep a `k` prefix and `CamelCase` after it
  (`kDefaultBranchLength`), whether public or private.
- Overrides of JUCE or other library virtuals keep the library's spelling
  (`processBlock`, `prepareToPlay`). You cannot rename them.
- No `m_`, `_`, or other prefix/suffix on members; the case alone says whether
  something is public or private.
- Always qualify names (`Genomosh::`, `juce::`, `std::`). Never write
  `using namespace`.
- Function names are verbs (`rebuildPlant`, `resetBranchLengths`,
  `syncGenerationParams`). Booleans read as questions (`isActive`,
  `hasEditor`).

## Whitespace and punctuation

- 4-space indent, no tabs.
- **No space between a function name and its opening parenthesis**, in calls,
  declarations, and definitions alike:
  `addFloat("volume", "Volume", 0.0f, 1.0f, 0.75f);`
  `void AudioPluginAudioProcessor::playNote(int midiNote, float velocity)`
- Control keywords DO get a space: `if (`, `for (`, `while (`, `switch (`.
- No space after `!`: `if (!uiMidi.isEmpty())`.
- No space in casts or templates: `static_cast<std::size_t>(branchIndex)`,
  `std::make_unique<juce::XmlElement>("SEED")`. Never use C-style casts.
- Lambdas: no space between the capture and the parameter list:
  `[this](int note, float velocity)`, `[&list](const juce::String& identifier)`.
- Spaces inside braces for brace-init lists and one-line bodies:
  `{ list.begin(), list.end() }`, `{ return true; }`.
- Pointer and reference symbols attach to the type:
  `juce::AudioBuffer<float>& buffer`, `const juce::String& identifier`,
  `auto* parameter`.
- Float literals always carry the `f` suffix (`0.0f`, `127.0f`).
- Target line length is about 100 characters (hard stop around 110).

## Spacing and alignment

- **Never pad code with extra spaces to line things up.** There is always
  exactly one space before and after `=` (and every other binary or compound
  assignment operator), and exactly one space between tokens elsewhere. Do not
  align `=` signs, values, arguments, or comments in columns across
  consecutive lines.

```cpp
// Correct.
state.macros.volume = raw("volume") * 1.15f;
state.macros.velSens = raw("velSens");
state.macros.tuningCents = raw("tuning");

addFloat("volume", "Volume", 0.0f, 1.0f, 0.75f);
addFloat("tuning", "Tuning", -100.0f, 100.0f, 0.0f);

const int totalNumInputChannels = getTotalNumInputChannels();
const int totalNumOutputChannels = getTotalNumOutputChannels();

// Wrong: padding to make columns line up.
state.macros.volume      = raw("volume") * 1.15f;
addFloat("volume", "Volume",    0.0f,    1.0f, 0.75f);
```

- When a call or expression wraps onto another line, the continuation line
  aligns under the first argument (this is indentation, not column padding):

```cpp
const auto grown = gmosh::resolveBranchGenome(lastPlant, branch,
                                               gmosh::clamp01(distance), traits);
```

## Braces

- Allman style: opening brace on its own line, for functions, classes, control
  statements, lambdas, and namespaces.
- A body that is a single one-line statement takes NO braces and goes on the
  next line:

```cpp
if (geneIndex >= gmosh::kNumGenes)
    return;

for (std::size_t geneIndex = 0; geneIndex < gmosh::kNumGenes; ++geneIndex)
    seedXml->setAttribute(gmosh::kGenes[geneIndex].id, static_cast<double>(seed[geneIndex]));
```

- A body that spans more than one line (including a nested control statement)
  takes braces.
- Trivial functions may be written on one line:
  `bool acceptsMidi() const { return true; }`.
- Tiny lambdas may be one line:
  `[this](const char* identifier) { return parameters.getRawParameterValue(identifier)->load(); }`.
  Anything longer uses Allman braces on separate lines.

## Blank lines

- One blank line above and below a control statement (`if`, `for`, `while`),
  except at the start or end of a block. An `if / else if / else` chain counts
  as one unit.
- A guard clause with an early return is followed by a blank line.
- Declarations at the top of a block are separated from the logic below them
  by a blank line.
- Functions are separated by a single blank line plus a section divider
  (below).

## Comments

- Section divider between groups of functions, exactly this line:
  `//==============================================================================`
- Comments go on their own line ABOVE the code they describe, with a blank line
  above the comment and none between the comment and the code. No trailing
  comments after code.
- Write full sentences in plain prose. Explain WHY, constraints, and invariants
  (threading, real-time safety, ordering), not what the code already says.
- Wrap comments at about 80 columns, using a continuation `//` on each line.
- Reference the design doc where a rule comes from, e.g.
  `(5.6.1: "branch lengths reset or decay")`.
- No commented-out code, no banner or ASCII-art comments beyond the divider.

## Language usage

- `const` by default on locals, parameters, and member functions that allow it.
- `auto` for obvious types, `const auto` for results you don't modify,
  `auto*` for pointers. Use `if (auto* parameter = ...)` to scope a pointer to
  its check.
- Smart pointers (`std::unique_ptr`, `std::make_unique`) for ownership. No raw
  `new`/`delete` except where JUCE requires it (e.g. returning from
  `createEditor`).
- Use `static_cast<>` for conversions. Use `juce::ignoreUnused(...)` for unused
  parameters.
- Audio thread rules: no allocation, no locks other than `juce::SpinLock` for
  tiny critical sections, no blocking calls inside `processBlock`.
- Include order: the file's own header first, then other project headers.

## Things to avoid (JUCE-template habits to NOT reproduce)

| Don't write | Write |
|---|---|
| `processorRef.playNote (note, velocity);` | `processorRef.playNote(note, velocity);` |
| `void Foo::bar (int value)` | `void Foo::bar(int value)` |
| `if (! showingGenes)` | `if (!showingGenes)` |
| `static_cast<float> (value)` | `static_cast<float>(value)` |
| `[this] (int note, float velocity)` | `[this](int note, float velocity)` |
| `for (int b = 0; ...)` | `for (int branchIndex = 0; ...)` |
| `auto* p = getParameter(...)` | `auto* parameter = getParameter(...)` |
| `int count   = 0;` (padded to align `=`) | `int count = 0;` |
| `foo(); // trailing note` | comment on the line above |
| `if (x) { doThing(); }` | `if (x)` newline `    doThing();` |
| no blank lines around `if` / `for` blocks | blank line above and below |