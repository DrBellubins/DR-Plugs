# GenoMosh — Codebase Map

An introductory guide to what each file, class and function in `Source/` actually does,
grouped by *the job it performs in the instrument* rather than by folder.

Scope: the tree as it stands at commit `f7c285a` (43 genes, 12 branches, 16 voices,
~7.0k lines including the test harness). Line counts are given in the appendix so you
can judge how much attention each part deserves.

Related documents: `Morphing synth.md` (authoritative design),
`Genetic Synth - Design Document for Local LLM.md` (reference-product estimation),
`Implementation-Status.md` (what was built when), `Failed_Design.md` §6.1.1
(the envelope model). Section numbers in the form *(doc §5.2)* refer to the design doc.

---

## 0. How to read the code

### 0.1 The two rules everything else follows

1. **The genome is always normalised `[0, 1]`.** Every real unit — Hz, seconds,
   semitones, percent — exists only in a *mapping layer*, and all the ranges and
   curves live in one data table. Mutation, persistence and the UI therefore all work
   in the same "gene space", and retuning the instrument means editing a table
   instead of hunting through the engine.
2. **The message thread precomputes; the audio thread only scales.** A branch is
   stored as a *direction vector*, never as a result. The audio thread computes
   `seed + direction × distance` once at note-on. That is what lets the mod wheel
   open a plant live with no allocation and no locks.

If you keep those two, everything else in the codebase falls out naturally. If you
drop them, the code stops being testable.

### 0.2 Data flow

```
MESSAGE THREAD                                     AUDIO THREAD
─────────────                                      ────────────
GeneTable.h  (what genes mean)
Curves.h     (gene space <-> physics)
Rng.h        (deterministic randomness)
        │
        └─ PlantEngine.buildPlant ──► PlantHandoff ──┐
PlantProcessor owns:                                │ (lock-free triple buffer)
  seed genome + MutationParams                      │
                                                  ▼
                                   Synth.startVoice(note)
                                     ├ branchForNote(note) = note % 12
                                     ├ PlantEngine.resolveBranchGenome(distance)
                                     └ VoiceParams.resolveVoiceParams  ← macros, velocity
                                                  │
                                                  ▼
                                   Voice.start + Voice.render
                                     ├ SharedEnv  (the one clock)
                                     ├ Modulator / Drift / SampleHold (free modulators)
                                     ├ MorphOsc ×2 + noise + sub
                                     └ TrapezoidSVF ×2 → amp → pan
                                                  │
                                                  ▼
                                   MasterFx (chorus → FDN) → softClip → out
```

### 0.3 Suggested reading order for a newcomer

1. `Genetic/GeneTable.h` — read the table itself. It *is* the instrument.
2. `Genetic/Curves.h` — how a number in `[0,1]` becomes Hz.
3. `Genetic/Mutation.h` + `PlantEngine.h` — read the header comments first, they
   describe the five-step growth algorithm.
4. `Engine/VoiceParams.cpp` — the single function where genes become music.
5. `Engine/Dsp.h` → `SharedEnv` — the envelope model.
6. `Engine/Voice.cpp` — everything assembled.
7. `Tests/EngineTests.cpp` — the contract all of the above must satisfy.

### 0.4 Layering, and why the folders are shaped this way

| Layer | Folder | JUCE? | Why |
|---|---|---|---|
| Genome & genetics | `Source/Genetic/` | **no** | Pure data in / data out; unit testable, drivable from a CLI *(doc §10)*. |
| Audio engine | `Source/Engine/` | **no** | Takes raw `float**` + note events so the same engine runs in the offline harness *(doc §13)*. |
| Host boundary | `Source/PluginProcessor.*` | yes | The only place parameters, presets and MIDI blocks exist. |
| Interface | `Source/UI/`, `Source/PluginEditor.*`, `Source/Utils/` | yes | Never touches DSP; reads snapshots. |

Because of this, `Source/Genetic/*.cpp` + `Source/Engine/*.cpp` are compiled into
both the plugin and the `GenoMosh_Tests` executable (`Tests/CMakeLists.txt`).
Nothing under `Genetic/` or `Engine/` may include a JUCE header.

---

## 1. Aspect: *Identity* — "what a gene is, and where its meaning lives"

Everything about timbre is data. No `switch` in the audio code knows what
`flt_q` means; the table does.

### `Genetic/GeneTable.h` — the gene table *(doc §4.3)*

The single most important file. It defines the genome's vocabulary.

* `GENO_MOSH_GENE_LIST(X)` — an X-macro that is the **single source of truth**: it
  generates both the `GeneId` enum and the `kGenes[]` table, so code and data can
  never drift apart. Columns:
  `id, label, curve, min, max, group, sensitivity, softLo, softHi, flags`.
  * `sensitivity` — how strongly this gene mutates (0 = never moves, 1 = free).
    **This column is where the taste of the instrument lives.**
  * `softLo/softHi` — the preferred region of `[0,1]`. Random seeds are drawn from
    inside it and branch displacement is compressed outside it, which is what keeps
    random results usable without ever hard-clamping.
  * `flags` — `kFlagNever` (never mutated), `kFlagRatio` (pitch offset, goes through
    the ratio snapper), `kFlagPitch` (can move the played pitch → bounded by
    Atonality).
* `enum Curve` — `Linear | Exp | Squared | Cubic | Bipolar`. `Exp` for anything that
  is a time or a frequency, `Bipolar` so that gene value `0.5` means "no effect".
* `enum Group` — `GrpAmp, GrpMod, GrpLoop, GrpLfo, GrpOscA, GrpOscB, GrpMix,
  GrpFilter, GrpFx, GrpMeta`. Groups exist for two reasons: *coherent mutation*
  (a branch moves a whole group at once) and the UI's column layout.
* `enum GeneId` + `using Genome = std::array<float, kNumGenes>` — the genome is a
  plain float array *(doc §3)*.
* `kGenes[kNumGenes]`, `kGroupNames[]` — defined in `GeneTable.cpp` from the macro.
* `geneGroup(gene)` — typed getter.
* `kNumBranches = 12` (one branch per pitch class), `kNumDirections = 3`
  (guard-rail retries, *(doc §5.6)*).

The gene groups in the current table: amp envelope (4) · mod envelope (4) ·
loop/tilt envelope (3) · LFO (3) · oscillator A (5) · oscillator B (6) ·
mix & cross-mod (5) · filter (5) · effects (6) · per-voice "life" (2) = **43**.

---

## 2. Aspect: *Determinism* — "the same patch must sound the same on every machine, at every sample rate"

### `Genetic/Rng.h`

Deliberately avoids `<random>` and `libm`: platform-dependent rounding would make
presets sound different on another machine *(doc §10)*.

* `hash::mix(z)` — splitmix64 finalizer.
* `hash::combine(a, b)` — combine two 64-bit values into a well-distributed third.
  This one function is how every identity in the system is derived: seed → branch →
  variant → voice.
* `hash::fromString(text)` — FNV-1a then mixed. Used for the initial fixed seed and
  for test seeds, so a named string always gives the same plant.
* `class Rng` — splitmix64 generator:
  `nextUint64/nextUint32`, `uniform()` → `[0,1)`, `range(lo,hi)`, `integer(n)`,
  `chance(p)`, `getState()`.
  * `gaussianClamped(scale)` — Irwin–Hall (sum of three uniforms), **not**
    Box–Muller: no `libm`, no platform-dependent rounding, and the clamp keeps a
    branch from ever getting an absurdly large kick.

---

## 3. Aspect: *Gene-space maths & boundaries* — "how a branch moves without breaking"

### `Genetic/Curves.h` *(doc §4.3, §5.2, §5.5)*

* `clamp01 / clampf / lerpf` — the helpers everyone else uses.
* `mapGene(GeneInfo, norm)` / `mapGene(int gene, float norm)` — normalised → real
  world, per curve.
* `unmapGene(...)` (both overloads) — the exact inverse. Needed to store host values
  back into genes, and tested for round-trip integrity.
* `softCompress(x, anchor, softLo, softHi)` — **the boundary policy.** Not clamping,
  not reflecting: displacement beyond the gene's soft range is exponentially
  compressed towards the hard limit. Three required properties *(doc §5.1)*:
  identity for `x ≤ softHi` (so distance 0 returns the seed exactly), strictly
  monotonic (so growing a branch is a smooth morph), never reaches 0/1 unless the
  seed was already there.
* `reflect01(x)` — the alternative suggested by the design doc, kept only for A/B.
  Rejected because reflection is continuous but *not monotonic*, so a growing branch
  visibly "bounces".
* `snapToRatio(semitones, atonality)` — the consonance ladder (unison, octaves,
  fifths, fourths, major thirds, harmonics 7/9/11 …). `atonality 0` = nearest table
  entry, `1` = raw free value. **The blend order is a design decision**: the snapped
  value is the atonality-0 end, because blending the other way makes every branch
  sing a random pitch when the atonality macro is closed.

---

## 4. Aspect: *Genetics* — "how a plant grows, and how it stays musical"

### `Genetic/Mutation.h` / `.cpp` *(doc §5)*

Pure data in / data out. Everything here is off the audio thread.

**Types**

* `struct MutationParams` — the tuning of the *process*, not of the sound:
  `coherence` (0 = every gene walks alone, 1 = a whole group moves as one),
  `distanceExponent` (gentle near the seed, wilder far out),
  `globalScale` (largest gene-space displacement at distance 1),
  `structuralRate` (how strongly discrete traits fire with distance),
  `atonality` (a **generation-time ceiling** on pitch-capable genes, not a
  modulation destination).
* `struct BranchDirection { Genome dir; uint64_t hash; float hue; float score; }` —
  a branch's fixed "direction of mutation". `dir` is pre-multiplied by sensitivity ×
  globalScale so the audio thread only scales it by the current distance.
* `struct BranchTraits { filterMode, fmDirection, modSource, oscBMode }` — the
  discrete topology choices of a branch. **This is what makes two branches of the
  same plant *modulate differently*, not just sound different.**

**Functions**

* `branchHash(seedId, branch, variant)` — deterministic identity of a branch.
  Every random choice in the engine is seeded from this chain.
* `buildBranchDirection(seedId, branch, variant, mp)` — *(doc §5.2 step 1)* one
  coherent Gaussian per group plus one per gene, lerped by `coherence`; scaled by
  that gene's `sensitivity`; pitch-flag genes further scaled by `atonality`.
* `applyDistance(seed, direction, distance, mp)` — *(steps 2–4)*
  `step = pow(d, distanceExponent)` → displace every gene → `softCompress` each one
  → `applyCouplings`. `d == 0` returns the seed **exactly**.
* `resolveTraits(hash, distance, mp)` — *(step 5, doc §5.4)* each trait has one
  fixed random threshold compared against `pow(d,3) · structuralRate`, so a trait
  flips **once** as a branch grows and never flips back. Deterministic *and*
  monotonic.
* `applyCouplings(Genome&)` — the guard rails, written as gene-space rules:
  resonance ceiling that tightens as cutoff rises and as drive rises; no downward
  filter mod under a low cutoff (it is just silence); never let both oscillators be
  pure sines with no noise; never let the amp envelope close everything off (a
  branch that cannot sound is a bug); don't let filter spread stack with resonance.
* `applyAtonality(Genome&, atonality)` — clamps every `kFlagPitch` gene to within
  `span × atonality` of its neutral point, then re-snaps `kFlagRatio` genes to the
  ratio ladder. **Written idempotent on purpose**: planting feeds a resolved genome
  back in as the next seed, and a non-idempotent ceiling would ratchet the tuning
  down a little further each time.
* `safetyScore(Genome)` — coarse "will this be audible and not horrible" scalar in
  ~[-1,1]. Used only to rank the `kNumDirections` variants of a branch.
* `loudnessCompensation(Genome)` — estimates sources of extra loudness
  (resonance², FM sidebands, sub, noise, drive) and returns `1/sqrt(boost)` so
  branches sit near the seed's level *(doc §5.6)*.
* `randomizeGenome(Genome&, Rng&, atonality)` — draws each gene inside its soft
  range; pitch-capable genes are drawn *inside* the atonality band rather than
  clipped into it afterwards, so a low-atonality seed still has a spread of tunings
  rather than a pile on the ceiling. Then biases toward instrument-like behaviour
  (audible attack, some sustain, filter open, FX pulled back) and runs couplings.

### `Genetic/PlantEngine.h` / `.cpp` — assembling & shipping the plant *(doc §3)*

* `struct PlantSnapshot { seedId, seed, directions[12×3], mutation, revision }` —
  an immutable bundle handed to the audio thread.
  `direction(branch, variant)` indexes it.
* `class PlantHandoff` — **lock-free triple buffer**, one writer (message thread),
  one reader (audio thread), preallocated:
  `beginWrite()` picks a slot that is neither published nor being read,
  `publish()` stores it with release semantics, `acquire()` (audio thread, once per
  block) swaps the active slot, `current()` returns it. No allocation, no locks,
  no clicks.
* `buildPlant(out, seedId, seed, mutation)` — applies the atonality ceiling to the
  seed (the ceiling belongs to the plant, not the voice), then builds all 36
  directions.
* `branchForNote(midiNote)` — `note % 12`, i.e. **the keyboard *is* the plant.**
  C4 and C5 play the same branch; the UI ring shows pitch classes *(doc §5.3)*.
* `resolveBranchGenome(plant, branch, distance, traitsOut)` — tries the direction
  variants, keeps the best `safetyScore` (early-out above 0.85), then re-applies
  `applyAtonality` because the *sum* of two gated things is not automatically
  gated. This is the guard-rail retry loop of *(doc §5.6)*.
* `effectiveDistance(branchLength, wheel, wheelScale)` —
  `clamp01(branchLength × wheel × wheelScale)` *(doc §5.3)*.

---

## 5. Aspect: *Mapping* — "what a gene means when it actually plays a note"

### `Engine/VoiceParams.h` / `.cpp` — the mapping layer *(doc §3, §4.1, §4.2)*

The boundary between gene space and physics. Runs **once per note-on**, never per
sample. Every hard ceiling of the instrument is declared here, which is what keeps a
wildly mutated branch playable.

* `struct MacroValues` — `volume, velSens, tuningCents, effect, wheelScale, wheel`.
  Macros are **not genes**: they never mutate, they scale what the player is already
  holding.
* `struct VoiceParams` — the fully-resolved playable snapshot of one voice
  (envelope `Shape`, mod-hold rate, LFO split into `vibSemis`/`tremDepth`,
  oscillator forms/ratios/noise, `aModSemis`/`bModSemis`, balance/FM/sub/detune,
  filter Hz·Q·key-follow·mod·spread, FX, drift/drive, `gain`, `velocity`, and the
  four trait bytes). A voice owns this, so growing a branch cannot change a held
  note.
* `resolveVoiceParams(genome, traits, macros, midiNote, velocity, out)` — the single
  function where genes become music. Read it top to bottom; the file's header comment
  documents the reinterpretation of the envelope genes. Notable decisions:
  * The envelope genes do **not** describe an ADSR; they describe *a pluck, plus a
    level decay over its repeats* (see §7 below).
  * `envShape.period` is clamped to `min(period, duration)` — without this, a slow
    `env_loop` leaves voices rising past the end of the note and the branch never
    arrives (it cost ~6 dB of level).
  * Pitch modulation depth is capped here (`kPitchModSemitoneCeiling = 12 st`,
    vibrato 12 st, tremolo 0.6), *not* in the voice.
  * Key follow is **precomputed for this note** (`(midiNote-60)/12` octaves), so the
    audio loop never does that subtraction.
  * Velocity is folded into `p.velocity` (amplitude) and `p.modScale` (modulation)
    once, per `VelSens` and `mod_vel`.
  * `gain = loudnessCompensation(genome) × velocity`.

  File-local constants (these are the instrument's safety envelope — worth
  re-deriving rather than copying blindly):
  `kInfiniteDurationGene = 0.9` (at or above this `env_time` there is no outer decay
  at all), `kPluckPeakFast/Neutral/Swell = 0.010/0.060/0.950`,
  `kPluckPeriodFast/Slow = 30 ms…16 s`, `kModHoldGate = 0.75` then 8 → 0.1 Hz,
  `kPitchModSemitoneCeiling`, `kVibratoSemitoneCeiling`, `kTremoloDepthCeiling`;
  helpers `expRange(lo,hi,v)`, `peakPositionFor(tilt)`,
  `curvatureExponent(gene) = 2^(2(v-0.5))`.

---

## 6. Aspect: *Sources* — "what actually oscillates, without aliasing"

### `Engine/Oscillator.h` *(doc §7)*

* `polyBlep(t, dt)` — PolyBLEP discontinuity correction (Välimäki/Stiles). Aliasing
  is called out in the design as the main reason digital synths sound cheap; this is
  the answer.
* `frac01(v)` — wrap to `[0,1)` for phase arithmetic.
* `class MorphOsc` — one band-limited oscillator.
  * `setFrequency(hz, sr)` — clamps `inc` to `[0, 0.45]`.
  * `setPhase / phaseValue / increment / reset` — phase control; the initial phase is
    randomised per voice, and `setPhase` is used by the audio-rate sync trait.
  * `render(p, form, colour, correctionPhase)` — `form ∈ [0,1]` morphs
    **sine → triangle → saw → pulse** across four slots with equal-power (sqrt)
    weights; every component is individually band-limited, so the morph itself
    cannot zipper.
  * `next(form, colour)` — advance then render.
  * `nextModulated(form, colour, phaseMod)` — the FM/PM path: render at a phase
    modulated copy of the current phase, but take the PolyBLEP correction at the
    *unmodulated* phase.
  * `heldValue(form, colour)` — the value at the start of the current period, i.e.
    the sample-and-hold used by the `oscBMode == 2` trait.
  * private `saw / pulse / triangle` — saw = corrected ramp, `colour` brightens it by
    adding an octave-up partial; pulse = two corrected saws subtracted with `colour`
    as pulse width; triangle = `1 − 2|saw|`, which reuses the saw's correction
    because the kinks land on the saw's discontinuities.

Noise (`a_noise`/`b_noise`) is *not* in this file — it is a single `whiteNoise()`
crossfade in `Voice.cpp`. Currently white; noise colouring is a known gap.

---

## 7. Aspect: *Time* — "one clock, two curves"

### `Engine/Dsp.h` → `class SharedEnv` *(doc §6.1.1, `Failed_Design.md` §6.1.1)*

The heart of the instrument, and **not an ADSR**:

```
amp(t) = pluck(phase)  ×  outer(t / duration)  ×  gate
mod(t) = pluckM(phase) ×  outer(t / duration)  ×  gate
```

* `struct Shape` — every decision the envelope makes arrives pre-made in this
  struct. `SharedEnv` derives **nothing**; all policy lives in the mapping layer.
  Fields: `period, peakPos, riseExp, fallExp, duration, outerExp, floorLevel,
  release, modPeak, modFallScale`.
* `kPluckFallExponent = 0.6` — the pluck's fall is deliberately **not a gene**.
  Giving it a gene is what turns the shape into a straight ramp and the voice into a
  click.
* `setSampleRate(sr)`, `setShape(s)` — resolves the shape into increments and
  **bakes two 512-entry wavetables** (level + modulator) so the audio loop does no
  `pow()`, and so moving a parameter can never click mid-note (a fresh note is
  always built from scratch). `period < 2 ms` is treated as audio-rate tremolo;
  `duration ≤ 0` means no outer decay at all.
* `gateOn() / gateOff() / reset()` — note-on restarts the clock at zero; note-off
  only switches the release stage, which fades *from the level it is at*.
* `process()` — advance phase and outer position, then
  `amp = curve(phase) · outer · gate` and `mod = modCurve(phase) · outer · gate`.
  Also handles self-termination: when the outer curve reaches its floor and that
  floor is ≈ 0 the stage goes Idle — a note with nothing to settle to ends by
  itself, click-free.
* `ampValue() / modValue()` — the two curves on the shared clock.
* `outerValue()` — the outer curve on its own: *a slow modulator that follows the
  note as a whole instead of following every pluck.* This is what `modSource == 1`
  selects.
* `isIdle()` — drives voice de-allocation.
* private `curveAt / buildTable / readTable / outerOf` — the curve itself
  (rise `u^riseExp`, fall `(1−w)^fallExp` inside `modFallScale` of the window) and
  the table reader. The outer curve is counted from the top down,
  `floor + (1−floor)·(1−q)^outerExp`, so it is exact at both ends and a floor of
  zero lands on exactly zero.

---

## 8. Aspect: *Modulation sources* — "the things that move, on their own"

All in `Engine/Dsp.h`. The distinguishing property is **free-running**: none of them
are scaled by the envelope, which is why a tremolo is still audible in the middle of
a slow note.

* `class SampleHold` — `setRateHz(hz)` (0 = pass through), `process(x)`, `reset()`.
  Used on the modulator (`mod_sh`). The step **is** the sound, so nothing here
  smooths it.
* `class Modulator` — the LFO: `setRateHz`, `setShape(shape, rng)` (sine / triangle
  / sample-and-hold, chosen per voice), `setPhase(p)` (random start per voice),
  `process()`. The S&H variant re-randomises once per period.
* `class Drift` — the slow smoothed random walk that makes a patch feel alive:
  `prepare(sr, rng, rateHz)` picks a new target every `1/rateHz` and smooths toward
  it with a ~250 ms one-pole. `Voice` runs two: one at 0.25 Hz (pitch) and one at
  0.11 Hz (filter).
* Also here: `kPi`, `softClip(x)` (cheap odd-symmetric saturator, unity slope at 0,
  bounded ±1 — used as the master output safety in `Synth::render`; the SVF has its
  own separate saturator inside its feedback loop), and `noteToHz(midiNote, cents)`.

---

## 9. Aspect: *Tone shaping* — "the filter, and why it stays stable at 18 kHz"

### `Engine/Dsp.h` → `class TrapezoidSVF`

A trapezoidal (zero-delay-feedback) state-variable filter: the classic coupled
integrator SVF digitised with the trapezoidal rule and the feedback loop solved
analytically. Unlike plain Chamberlin it stays stable and accurate right up to
Nyquist, which matters because `flt_freq` reaches 18 kHz.

* `setSampleRate(sr)`, `setCutoffQ(hz, q)` — precomputes
  `g = tan(π·fc/fs)` and the solved coefficients; clamps fc to 0.96·Nyquist and
  Q ≥ 0.3 (no self-oscillation blow-up).
* `setDrive(d)` — saturation *inside* the feedback loop.
* `process(x, lpMix, bpMix, hpMix)` — one iteration, returns the weighted mix of the
  three classic responses; the `filterMode` trait chooses the weights.
* `reset()`.

### `Engine/Dsp.h` → `class DcBlocker`

One-pole highpass at ~7 Hz. Mandatory: resonant SVFs and ring modulation both store
DC, and the FDN integrates it.

---

## 10. Aspect: *Space* — "the master effects"

### `Engine/MasterFx.h` / `.cpp` *(doc §8)*

Chorus into an 8-line **feedback delay network** reverb. One shared wet bus for the
whole instrument; its parameters follow the timbre of whatever was just played.

* `prepare(sr, maxBlockSamples)` — allocates everything up front, scales the base
  delay lengths by `sr/48k`, and installs the input/output gain rows of the Hadamard
  matrix.
* `reset()` — clears every delay line, damper and modulator.
* `setParams(size, rt60, damp, chorus, mix, width)` — sets *targets* only; values
  are ramped inside `process` so growing a branch never clicks the tail.
* `process(left, right, n)` — the whole chain, per sample:
  1. exponential parameter smoothing (~80 ms) and, every 32 samples,
     `updateLineLengths()`;
  2. chorus stage: two decorrelated modulated taps, depth *and* rate scaled together
     by `fx_chors`;
  3. FDN: read each line (with slow sine length modulation to kill metallic
     ringing) → per-line damping → Hadamard mix → feedback gain → push
     `yOut + dry×inputGain`;
  4. wet pickup through the output gain rows, `fx_width` applied to **side only** so
     the dry stays centred, equal-power wet/dry sum.
* `tailSeconds()` — feeds `getTailLengthSeconds()`.
* private `updateLineLengths()` — `fx_size` → lengths, `fx_len` →
  `rt60ToFeedback(L, RT60) = 10^(−3L/RT60)`, `fx_damp` → per-line one-pole cutoff
  (18 kHz → 700 Hz).
* file-local `kHadamard[8][8]` (Sylvester: symmetric and orthogonal, so the network
  stays energy-preserving at any gain), `kBaseLength[8]` (mutually prime lengths at
  48 kHz — primeness is what stops an FDN ringing like a bell), `kInvSqrt8`.
* `class VariableDelay` (in `Dsp.h`) — the fractional delay line: `prepare`,
  `push`, `read(delaySamples)` with linear interpolation and wrap-around,
  `size()`, `reset()`.
* `class OnePole` (in `Dsp.h`) — `setHz(hz, sr)` + `process(x)`, used as the FDN
  line dampers.

---

## 11. Aspect: *Polyphony & scheduling* — "notes to branches to voices"

### `Engine/Synth.h` / `.cpp` *(doc §5.3, §7)*

16-voice pool. Deliberately free of JUCE types (raw `float**`, note events as ints)
so the offline harness can drive it *(doc §13)*.

* `struct PlayState { MacroValues macros; std::array<float,12> branchLength; }` —
  the per-block state the message thread fills.
* `prepare(sr, maxBlock)` / `releaseResources()` — allocates voices, FX and scratch
  buffers, and `reserve()`s the event vectors so the audio thread never allocates.
* `attachPlant(PlantHandoff*)` — the only connection to the genetics side.
* `setPlayState(state)` — called at the top of every block.
* `addNoteOn(note, vel, sampleOffset)` / `addNoteOff(note, sampleOffset)` /
  `allNotesOff()` — events are *queued*, not applied, so they can be applied at the
  right sample.
* `render(channels, numChannels, n)` — the block scheduler:
  `plant->acquire()` → stable-sort pending events → render the block **in segments**
  between event offsets (sample-accurate note starts) → run `fx.process` → apply
  `volume` and `softClip` as runaway protection.
* `renderSegment(...)` — mix active voices into the channel pointers; when the host
  gives one channel, render to scratch and fold to mono.
* `applyPending(event)` — note↔voice bookkeeping. A re-trigger of a note that is
  already held **re-articulates its own voice** rather than adding a second tracking
  entry (this used to grow `heldNotes` without bound while a branch was dragged).
* `startVoice(i, note, vel)` — the whole runtime path in nine lines:
  `branchForNote` → `effectiveDistance` → `resolveBranchGenome` →
  `resolveVoiceParams` → `Voice::start` → push FX params from the same
  `VoiceParams` (the tail follows the timbre that was just played).
* `allocateVoice()` — free voice, else **release-aware stealing**: steal the
  quietest, measured with `Voice::ampLevel()`, a slow peak follower. The
  instantaneous amp is useless here — a plucked voice sits at zero twice per cycle.
* `numActiveVoices() / numHeldNotes() / lastBranch()` — debug + UI.

---

## 12. Aspect: *Host boundary* — "parameters, presets, MIDI, seeds"

### `PluginProcessor.h` / `.cpp`

Owns the seed and the plant; owns nothing to do with sound generation.

**Parameter model** (`createParameterLayout`) — 19 parameters:
`volume, velSens, tuning, atonality, effect, wheel, wheelScale` (macros) and
`branch1 … branch12` (lengths, default `kDefaultBranchLength = 0.45`, automatable so
a host can grow a branch over time). **The seed genome is *not* a parameter list** —
it is patch state saved as XML. That split is deliberate (macros are live and
automatable; the genome is the patch).

**Lifecycle** — `prepareToPlay` (prepares `Synth`, attaches the handoff),
`releaseResources`, `isBusesLayoutSupported` (mono or stereo out), `processBlock`,
`getStateInformation` / `setStateInformation`.

**Plant ownership**

* `rebuildPlant()` — `handoff.beginWrite()` → `buildPlant` → keep a message-thread
  copy in `lastPlant` for the UI → `publish()`. (The UI must never read the
  handoff: `activeSlot` belongs to the audio thread.)
* `setSeedGene(gene, v)` / `setSeedGenome(g)` — used by the gene grid; both rebuild.
* `newSeed()` — walks a **deterministic seed chain** (`++seedId; hash::mix`) so the
  same sequence of presses gives the same plants on every machine, randomises inside
  the atonality ceiling, and **folds the twelve branch lengths back to default** (a
  new plant starts as a seed, not as a seed wearing the previous plant's growth).
* `plantBranch(branch, distance)` — resolve that branch and feed it back as the new
  seed, with lengths reset *(doc §5.6.1)*.
* `resetBranchLengths()` — because branch lengths are patch state, re-seeding has to
  clear them explicitly.
* `syncGenerationParams()` / `parameterChanged("atonality")` — atonality is
  generation-facing: changing it rebuilds the plant, it never retunes a note that is
  already sounding.
* `fillPlayState(state)` — reads raw parameter values into a `PlayState`, resolves
  "host wheel parameter vs incoming CC1", and publishes `liveWheel` for the UI.
* `handleMidiController(msg)` — CC1 → `midiWheel` atomic.
* `playNote/stopNote` + `uiMidi` behind a `SpinLock` — lets the UI keyboard and the
  plant view inject notes into the next audio block.
* `seedGenome()/seedId()/lastPlantSnapshot()/currentWheel()/lastPlayedBranch()` —
  read-only accessors for the editor.
* `branchParamId(b)` — one place where the parameter id strings live, so the UI and
  the audio code agree.
* Persistence — `<GenoMoshState version mutationVersion>` + the parameter
  `ValueTree` XML + `<SEED seedId>` with one attribute **per gene id string**.
  Keyed by name, so renaming/reordering genes in the table is survivable;
  `version`/`mutationVersion` exist because the table *will* change and no
  migration path is written yet.

---

## 13. Aspect: *Interface* — "the plant on screen"

### `UI/PlantView.h` / `.cpp` *(doc §12)*

The instrument's face: a ring of twelve slots = twelve pitch classes = exactly what
`branchForNote` does on the audio side.

* `update(snapshot, lengths, activeBranch, wheel)` — called from the editor's timer;
  the view never computes DSP.
* `paint(g)` — draws the ring, then for each slot: the scaled tree, the growth
  handle at the radius that branch reaches with the wheel fully open, and the pitch
  class label (the ring is the keyboard and stays put). Plus a gentle idle sway so
  the plant is never completely still, and the seed marker at the centre.
* `growTree(out, from, angle, length, depth, rng, reach)` — recursive quadratic
  Béziers in **unit space** (origin at the seed, root length 1), seeded from
  `direction.hash`, tracking `reach` from the three control points of each stroke
  (a Bézier lives inside their convex hull, so bounding those bounds the stroke).
* `drawPlant(...)` — grows in unit space, then scales by `targetReach / reach`.
  **This normalise-after-drawing is what guarantees the plant can never leave the
  ring for any branch length or wheel value**, without clamping its shape.
* `branchColour(direction, hueShift)` — hue from the branch hash, saturation from
  its cutoff displacement, brightness from its waveform displacement: the drawing is
  a deterministic fingerprint of the sound, not an attempt at botany.
* `slotAt(position)` → `noteForSlot(slot)` = `60 + slot` — hit-testing that agrees
  with the audio mapping by construction.
* `mouseDown/mouseDrag/mouseUp` — left drag writes the branch's parameter from the
  pointer radius; right click calls `onPlantBranch` (planting).
* Note gating: `startNote / endNote / releaseAllNotes` + `timerCallback`.
  A **clicked** slot sounds while the button is held; a **dragged** slot sounds only
  while the branch is actually being changed — stop moving for 250 ms
  (`kIdleReleaseMs`) and the note is released, move again and it re-articulates.
  Only one note is ever held by this view, which is what prevents stuck notes.
* `onPlayNote(note, velocity)` / `onPlantBranch(branch)` — callbacks; velocity 0
  means note-off (the same convention the computer keyboard uses).

### `UI/GeneGrid.h` / `.cpp` — the gene table made editable

Every gene as a horizontal slider, grouped by `Group`, in a two-column scrollable
grid. This is the "make the gene table tunable by ear" debug page — the tool you use
to tune `sensitivity` and the soft ranges.

* ctor — builds one row per gene from `kGenes[]`, one header per group.
* `refresh()` — pull the current genome into the sliders.
* `sliderValueChanged(slider)` — map a slider back to its gene and call
  `setGeneValue(gene, value)`.
* `placeColumn(col, firstGroup, lastGroup, x, w)` / `resized()` — layout driven
  entirely by the table's group order, so the grid follows the table when it changes.

### `UI/ComputerKeyboard.h` / `.cpp` — playing without a DAW

Two piano-like rows of letters (`z s x c …` → C3, `q 2 w e …` → C4), comma/period
transpose. Ported from the Chronoverb key map.

* `handleKeyPress(kp)` — returns true when the key belongs to this keyboard.
* `handleKeyStateChanged()` — the release path: JUCE only reliably reports key-ups
  here, so this polls `KeyPress::isKeyCurrentlyDown` for **just the held keys**.
* `releaseAll()` — focus lost / editor closing.
* `mappings() / normalise() / find() / noteFor()` — the static key map.
* `helpText()` — the one-line hint shown in the editor.

### `PluginEditor.h` / `.cpp` — the window

* ctor — attaches knobs/sliders to `parameters` with `SliderParameterAttachment`,
  wires `plantView`/`computerKeys` to the processor, sets up captions and tooltips
  (each macro carries a tooltip explaining what it does — that text is the best
  short description of the macros in existence).
* `showPage(bool genes)` — swaps between the plant page and the gene-table page.
* `layout()` — **one** function returns every rectangle; `paint()` and `resized()`
  both call it, so the dark plates drawn behind the live controls always land exactly
  where the widgets go. The layout itself encodes the design: *top row / plain
  background = seed generation, bottom row / dark plate = live macros.*
* `timerCallback()` — pushes branch lengths, the last-played branch, the current
  wheel and the wheel-scale readout (wheel × scale, otherwise invisible) into the
  plant view; releases computer-keyboard notes when focus leaves.
* `keyPressed` / `keyStateChanged` / `updateKeyHint()` — computer-keyboard plumbing.
* `paint` / `resized` — background, header rule, control plates.

### `Source/Utils/` — shared widget furniture (not synth-specific)

Carried over from the DR-Plugs template; `Theme.h` + `FlatRotaryLookAndFeel` +
`ThemedKnob` are the only parts the current UI uses. `FlatLabel`, `ThemedCheckbox`,
`{Horizontal,Vertical}RangeSlider(+Attachment)` and `SteppedHorizontalRangeSlider`
are currently **unreferenced** by GenoMosh's own UI — useful to know when deciding
what to re-implement.

* `Theme.h` — the palette (backgrounds, plate greys, accent pink).
* `FlatRotaryLookAndFeel` — flat rotary slider drawing.
* `ThemedKnob` — rotary with a label and injectable value↔text conversion.
* `{Horizontal,Vertical}RangeSlider` + `...ParameterAttachment` — dual-value range
  widgets bound to two APVTS parameters; `SteppedHorizontalRangeSlider` adds
  quantised steps (the widget any `Curve::Stepped` gene would need).

---

## 14. Aspect: *Verification* — the offline harness

### `Tests/EngineTests.cpp` (1265 lines, plain `main()`, no framework, no JUCE)

This is the design doc's §13 measurement harness, and it is the clearest statement
of what the code is *for*. `check(ok, what)` prints PASS/FAIL and counts failures;
`struct RenderResult` + `renderBranch(plant, mp, branch, distance, note, sr)` drive
the real `Synth` offline and return peak/rms/dc.

| Test | What it pins down |
|---|---|
| `testCurves` | every gene's normalised↔real round trip and its endpoints, for every curve type |
| `testRatioSnapping` | atonality 0 snaps / 1 leaves free; snapping is a fixed point; deviation shrinks monotonically with atonality |
| `testGenome` | bit-identical directions from a seed id; **distance 0 returns the seed exactly**; deviation grows monotonically with distance; 60 random seeds × all branches at distance 1 stay finite, in [0,1], and a majority pass `safetyScore` |
| `testAtonalityCeiling` | the ceiling actually bounds pitch-flag genes, and is idempotent |
| `testPitchStability` | "nothing *else* moves the pitch" (its seed pins `a_mod`/`b_mod` neutral) |
| `testEnvelopeMapping` | what the `vol_*`/`env_*` genes resolve to in the mapping layer |
| `testPluckEnvelope` | what `SharedEnv` does with them — a pluck, repeated, with a level decay over the repeats |
| `testModulationReachesAudio` | pitch modulation is real and bounded; tremolo moves the level without moving the mean |
| `testFilter` | the trapezoidal SVF against an RBJ biquad reference |
| `testRender` | every branch audible, no NaN/Inf, no runaway DC, branches within ~12 dB of each other, bit-identical reruns, clean at 44.1 / 48 / 96 kHz |
| `testNoteBookkeeping` | what dragging a branch does to the voice pool (re-articulate one voice, release once) |

Build & run:

```sh
# cmake target (also runs under ctest)
cmake -S . -B cmake-build-debug -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja
cmake --build cmake-build-debug --target GenoMosh_Tests
./cmake-build-debug/GenoMosh_Tests

# or directly, no build system (the -I is required: the harness includes by
# absolute-ish path, "Engine/Synth.h")
g++ -std=c++17 -O2 -I Source Source/Genetic/*.cpp Source/Engine/*.cpp \
    Tests/EngineTests.cpp -o /tmp/gmosh_tests -lm
```

Current state at this commit: **56 checks, 0 failures.** The rendered-branch table it
prints (peak / rms / DC per branch) is the closest thing the project has to a
regression baseline — if a change moves a branch's rms by more than a few dB, that is
a design decision, not a bug fix. Note also that a *stale* test binary will quietly
run an older suite; rebuild before trusting a green result.

---

## 15. Invariants the code guarantees (and where they are enforced)

Keep these in any reimplementation; they are the contract.

| Invariant | Enforced in | Tested by |
|---|---|---|
| Genome is always in `[0,1]` | `softCompress`, `applyCouplings` | `testGenome` |
| Distance 0 returns the seed **exactly** | `applyDistance` early-out, `softCompress` identity | `testGenome` |
| Growing a branch is monotonic & continuous | `Curves.h::softCompress`, threshold-based `resolveTraits` | `testGenome` |
| Same seed id ⇒ bit-identical plants | `Rng.h`, `branchHash`, seed chains | `testGenome`, `testRender` |
| Works at any sample rate | every block takes `sampleRate` | `testRender` |
| Nothing in a note can exceed its pitch ceiling | `applyAtonality` (generation) + `kPitchModSemitoneCeiling` (mapping) | `testAtonalityCeiling`, `testModulationReachesAudio` |
| Every branch makes an audible, non-horrible sound | `applyCouplings`, `safetyScore`, `randomizeGenome` biases | `testRender`, `testGenome` |
| Branches stay near the seed's level | `loudnessCompensation` | `testRender` (±12 dB) |
| A held note is never changed by growth | voices own their `VoiceParams`/envelope tables | design, by construction |
| No allocation on the audio thread | `prepare()` everywhere, `reserve()`, triple buffer | by construction |
| No clicks when parameters move | control-rate ramping (`Voice`), block-rate smoothing (`MasterFx`) | `testRender` (no discontinuity/DC checks) |

---

## 16. Known gaps (things the current code does *not* do)

Useful as a wish-list for a reimplementation; all of these are known and accepted.

* No oversampling on the saturators, FM or filter — the off-grid/aliasing metric in
  the harness does not yet tolerate an octave per partial, so M6-style voice quality
  work is still open.
* One filter topology (the single SVF response pair); no `flt_type` gene, no
  state-variable topology variety, no shelves, no output trim (`adj_*`) genes.
* No reverb attack behaviour (`rvb_atk`) and no wet gate; one shared wet bus whose
  parameters follow the last note played.
* No pitch bend, sustain pedal, MIDI-learn/`IMidiMapping`, mono/legato/portamento,
  or tempo-synced LFO.
* Seed id is an opaque u64: no human-readable seed codes, no seed import/export UI.
* State XML is version 1 with no migration path, even though the gene table has
  already changed shape since it was written.
* The gene grid edits genes by rebuilding the whole plant on every change
  (fine for a debug page, not for a smooth UI).

---

## 17. Appendix — file inventory

```
Source/
  Genetic/                     JUCE-free. The genetics.
    GeneTable.h   152  the gene table (X-macro) + enums + Genome
    GeneTable.cpp  25  kGenes[] / kGroupNames[] generated from the macro
    Curves.h      157  mapGene/unmapGene, softCompress, snapToRatio
    Rng.h          97  splitmix64, FNV hash, clamped Irwin-Hall gaussian
    Mutation.h    109  MutationParams, BranchDirection, BranchTraits, API
    Mutation.cpp  257  directions, distance response, couplings, guard rails
    PlantEngine.h  72  PlantSnapshot, PlantHandoff, resolveBranchGenome
    PlantEngine.cpp 109 buildPlant, branchForNote, guard-rail retries
  Engine/                      JUCE-free. The audio.
    Dsp.h         522  OnePole, TrapezoidSVF, DcBlocker, VariableDelay,
                       SharedEnv, SampleHold, Modulator, Drift, softClip
    Oscillator.h  146  PolyBLEP + MorphOsc (equal-power waveform morph)
    VoiceParams.h  81  MacroValues, VoiceParams
    VoiceParams.cpp 193 resolveVoiceParams — the mapping layer
    Voice.h        96  one voice: sources, filters, control-rate state
    Voice.cpp     319 note start, modulation bus, per-sample render
    MasterFx.h     69  chorus + 8-line FDN
    MasterFx.cpp  206 Hadamard FDN, damping, width, equal-power mix
    Synth.h        92  voice pool, PlayState, note events
    Synth.cpp     264 scheduling, branch mapping, stealing, output
  UI/
    PlantView.h   117 / PlantView.cpp 316   the plant ring, growth dragging
    GeneGrid.h     54 / GeneGrid.cpp  143   gene table editor
    ComputerKeyboard.h 69 / .cpp     140    computer-key playing
  Utils/                                    theme + generic widgets
  PluginProcessor.h 120 / .cpp 337          host boundary, seeds, presets
  PluginEditor.h   85 / .cpp     404        window, layout, live readouts
Tests/
  EngineTests.cpp 1265                      the acceptance harness (§14 above)
                              ─────────
                              ~7.0k lines total
```
