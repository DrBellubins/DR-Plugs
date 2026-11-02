# GenoMosh — Implementation Status

*Report format requested by the design document, section 14: what was built,
what was assumed, measured results, open questions.*

*New to the code? Start with `Codebase-Map.md` — it walks the tree by aspect
(what each file, class and function does) and lists the invariants the engine
guarantees.*

Date: 2026-10-20. Branch: first working pass, covering phases 1–6 in rudimentary
form (genome, mutation, voice, modulation, FX, plugin shell) plus a debug UI.

---

## 1. What was built

```
Source/Genetic/     pure data, no JUCE (unit testable core, design doc 10)
  Rng.h             splitmix64 + FNV hash + clamped Irwin-Hall gaussian
  GeneTable.h/.cpp  the gene table, generated from one macro: enum + data table
  Curves.h          normalized <-> real mapping, softCompress, ratio snapping
  Mutation.h/.cpp   branch directions, distance response, couplings, guard rails
  PlantEngine.h/.cpp PlantSnapshot + lock free triple buffer handoff

Source/Engine/      audio, no JUCE (raw float** + note events)
  Dsp.h             one pole, trapezoidal (ZDF) SVF, DC blocker, fractional
                    delay, envelopes, modulators, drift random walk
  Oscillator.h      PolyBLEP saw/pulse/triangle + sine, equal power form morph
  VoiceParams.h/.cpp genome + macros + note -> playable settings
  Voice.h/.cpp      2 oscillators, PM/FM, sync/ring/S&H, noise, sub, dual
                    filter, amp + mod + loop envelopes, LFO, 2 drift sources
  MasterFx.h/.cpp   chorus into an 8 line Hadamard FDN reverb
  Synth.h/.cpp      16 voice pool, note -> branch mapping, guard rail retries

Source/UI/          PlantView (ring + plant + drag to grow), GeneGrid (43 sliders)
Tests/EngineTests.cpp  offline harness: engine checks + offline render
```

Genome: **43 genes** (the 38 of the design document, plus `vol_rel`, `b_color`,
`det_spread`, `drift_amt`, `sat_drive`, `fx_width` — the "candidates" listed in
4.2). 12 branches × 3 direction variants for the silence re-roll.

UI skeleton (not the full plant UI): a ring of the 12 pitch classes around a
drawn plant, drag outwards to grow a branch, click a segment to play it, right
click to plant a branch; four macro knobs (tuning, atonality, effect,
rotation); three bottom sliders (wheel scaling, velocity sens., volume) as in
the reference screenshot; a "genes" page with every gene as a slider; new seed
and plant branch buttons.

## 2. Assumptions and deviations from the document

| Item | What was done | Why |
| --- | --- | --- |
| 5.2 step 4 boundary handling | `softCompress` instead of `reflect01` | reflection is continuous but not monotonic, so growing a branch visibly bounces; softCompress is monotonic and provably returns the seed exactly at distance 0. `reflect01` is kept in `Curves.h` for A/B. |
| 5.4 structural mutations | traits are *thresholds* drawn once per branch, compared against `d^3 * rate` | makes them deterministic **and** monotonic in distance: a trait flips once as a branch grows and never flips back. Not crossfaded yet (voices capture their genome at note on, so held notes do not click). |
| 5.6 silence rejection | 3 precomputed direction variants per branch, pick the best `safetyScore`, bounded retries | faithful to "re-roll with the next seed in the hash chain". |
| 6.3 modulation matrix | not implemented; fixed per destination amounts (`a_mod`, `b_mod`, `fm_mod`, `flt_mod`) plus a *source selector* (`modSource` trait) | smallest thing that gives evolving modulation; the weighted matrix is the next step. |
| Wheel semantics | `d = branchLength * wheel * wheelScale`, `wheel` defaults to 1.0 and is both a host parameter and CC1 | the document's formula, but a plant that starts closed is unusable in a DAW without a wheel; defaulting to open keeps it playable. |
| Filter | trapezoidal (ZDF) SVF, two cascaded stages, `flt_sep` spreads their cutoffs | verified against a reference biquad (see 3). |
| `b_sh` | interpreted as oscillator **shape mode**: hard sync / ring mod / sample and hold, chosen by the structural mutation | the document marks this gene [unknown]. |
| TriggerA–H | not implemented | [unknown] in the document. |
| Programs / preset browser | single program, state via XML (params + seed genome + seedId + mutationVersion) | preset management is a later phase. |
| Sustain pedal, pitch bend, MIDI channel config | not implemented yet | section 9 is not started. |

## 3. Measured results (`ctest`, `GenoMosh_Tests`, 48/44.1/96 kHz)

```
[PASS] normalized <-> real round trip for every gene
[PASS] same seed id gives bit identical branch directions
[PASS] distance 0 returns the seed exactly
[PASS] deviation from the seed grows monotonically with distance
[PASS] all resolved genomes stay finite and inside [0,1]
       usable branches at distance 1: 720 / 720
[PASS] trapezoidal SVF matches a reference lowpass within 1 dB   (0.00 dB measured)
[PASS] SVF stays stable at fc 15 kHz, Q 8
       12 branches at distance 0.85: peak 0.55..0.64, rms 0.073..0.076, |dc| < 6e-4
[PASS] no NaN/Inf, every branch audible, branches within ~1 dB of each other
[PASS] rendering is bit identical across runs
[PASS] renders cleanly at 44.1 / 96 kHz
```

Level compensation (5.6) is working: 12 far branches land inside ~1 dB.

## 4. Known weak points / next work

1. **Timbre tuning.** The gene ranges have never been listened to. The test
   harness prints levels but not spectra; the real work of this project
   (design doc 16, priority 1) is still ahead.
2. **Waveform morph is a crossfade of four shapes**, not a true band limited
   morph; `a_color` adds an octave up partial. Both are placeholders.
3. FM is phase modulation with a fixed index, no oversampling (7 suggests 2–4x
   on the FM path).
4. Structural traits are not crossfaded inside held notes.
5. No sustain pedal / pitch bend / MIDI config (section 9).
6. `PlantView::update` copies a whole `PlantSnapshot` (≈6 KB) at 30 Hz on the
   UI thread; fine, but it should only need the hue/hash fields.
7. Voice stealing restarts the stolen voice immediately instead of fading it.

## 5. Questions for the operator

1. Wheel semantics confirmed as "closed at rest, opens with the wheel"? The
   default here is *open* so the plugin is usable without a controller.
2. Is `b_sh` sync/ring/S&H a reasonable reading, or should it be a single
   continuous control?
3. Should branch lengths be MIDI learnable, and should planting a branch reset
   the branch lengths (currently it does not)?
4. Polyphony budget: 16 voices is the current constant.

---

## 6. Addendum 2026-10-21 — playing the plant, and stuck notes

Two reported problems: dragging a branch left a note sounding forever, and the
held note slid down in pitch. Plus a request for the Chronoverb computer
keyboard playing.

### Stuck note (real bug)

`PlantView::mouseUp` only sent the note off when the press had **not** turned
into a drag, so every drag left a voice gated on with the amp envelope sitting
at `vol_sus`. Fixed by making the view own exactly one note (`heldNote`) and
always ending it on mouse up.

Note gating now follows the reference instrument:

| interaction | sound |
| --- | --- |
| click and hold a segment | sounds while the button is held |
| drag a branch | sounds **only while the branch is being changed**: the note is released 250 ms after the pointer stops moving, and re articulates when movement resumes |
| mouse up, view hidden, editor closed | note off, always |

The idle gate is a 30 Hz `juce::Timer` inside `PlantView` that only runs while
a note is held (`kIdleReleaseMs`, `kMoveThresholdPx` in `PlantView.h`).

### Second bug found on the way (`Synth::applyPending`)

A note on for a note that was already held pushed a **second** tracking entry
into `heldNotes`, so one note off left a stale entry behind and repeated re
triggering grew the vector on the audio thread. One slot per note now, reused;
a re trigger re articulates its own voice instead of allocating another.

New regression test (`ctest`):

```
note bookkeeping
  [PASS] repeating note on for one note keeps exactly one held note
  [PASS] re articulating a held note does not spend extra voices
  [PASS] one note off releases the note completely
  [PASS] a stray note off is harmless
```

`Synth::numHeldNotes()` was added as the debug/harness hook for this.

### The pitch slide is not a bug

**Correction (later the same day): it is a bug.** The reasoning below was wrong
— the slide is not "intended modulation", it is the mod envelope being wired to
oscillator pitch, which 5.3 forbids. See section 8.

With a gate stuck open you hear the whole mod envelope travel: `a_mod` /
`b_mod` are bipolar ±1 octave and `Voice::updateControlTargets` scales them by
`modMix * 0.75` octaves, while `modMix` walks from +1 to −1 as the mod
envelope decays (`mod_dcy` reaches 12 s). So a long held note glides down by
up to ~0.75 octave, with `drift_amt` adding a slow ±12 cent wander on top.
That is the intended modulation, just never meant to be heard in full. If it is
too much, the one constant to turn down is the `0.75f` in
`Voice::updateControlTargets`, or lower `a_mod`/`b_mod` on the genes page.

### Computer keyboard playing

`Source/UI/ComputerKeyboard.{h,cpp}` is the Chronoverb
`ComputerKeyboardSquareSynth` key map, without its oscillator: two piano like
rows (`z s x c v b n m` = C3..B3, `q 2 w e 5 r t 6 y u` = C4..B4), octave
transpose on `,` and `.`. It only produces note on/off through a callback, so
the editor wires it to `playNote`/`stopNote` exactly like the plant view.

The editor hooks `Component::keyPressed` / `Component::keyStateChanged` rather
than a `KeyListener`: JUCE walks those from the focused component up to the
editor, so the keys work with a slider focused too, and it avoids the
`-Woverloaded-virtual` hiding warnings the KeyListener route produces.
Releases come from polling `KeyPress::isKeyCurrentlyDown` for the keys we are
holding only (the original polled the whole map). Notes are let go when focus
leaves the editor and in the editor destructor. The key map is printed in the
footer with the current octave.

Because notes go through the normal note path, playing on the computer keyboard
lights up the branch it maps to on the ring.

---

## 7. Addendum 2026-10-21 — six UI / behaviour reports

All six were reported from playing the standalone. Five were real bugs, one was
a missing affordance.

### 1. "new seed" left the old plant's growth in place (real bug)

`newSeed()` re-randomized the genome but not the twelve branch length
parameters, so a fresh seed appeared wearing the previous plant's growth.
`AudioPluginAudioProcessor::resetBranchLengths()` now folds them back to
`kDefaultBranchLength` (0.45) from `newSeed()`, and from `plantBranch()` too —
5.6.1 says planting means "branch lengths reset or decay", and the old code did
neither. The editor also clears its `selectedBranch` and refreshes the gene
page when a new seed is grown.

### 2. Atonality was inverted (real bug, the important one)

`snapToRatio` returned `lerpf (raw, snapped, atonality)`, i.e. atonality 0 gave
the **raw** gene value and atonality 1 gave the **snapped** one — exactly
backwards from 5.5. With `a_freq`/`b_freq` drawn anywhere in ±14 semitones,
"atonality 0" therefore meant every branch sang a random pitch, which is the
one thing that guard rail promises not to do.

Fixed to `lerpf (snapped, raw, atonality)`, and the macro default dropped from
0.65 to 0.25 so a random plant is tuneful by default as 5.5 asks.

New test (`testRatioSnapping`, run first in the harness): at atonality 0 the
result is a fixed point of the snapper, never more than 2.5 semitones from the
input, and 193 free values over ±24 semitones collapse onto 23 distinct
offsets; at atonality 1 the function is the identity; the blend moves
monotonically between the two ends.

Note what "in tune" means here: the branch still contributes its own oscillator
offsets, they are just consonant ones (unison, octaves, fifths, harmonic
series entries) instead of arbitrary cents. Turning atonality fully left is the
way to hear the keyboard pitch itself.

### 3. Rotation did something invisible (missing affordance)

The mapping was always correct (`branchForNote` and the ring labels agreed), but
the ring labels were the only thing that moved, and twelve 13 px glyphs moving
by 30° is not visible. The plant view now treats the ring as the keyboard — slot
*i* always shows pitch class *i* — and rotates the **plant** underneath it,
which is what 12 describes ("12 radial branches offset by `rotation`"). Turning
the knob spins the whole plant to the keys that now play those branches, and the
knob reads `+3` instead of `3.00`.

`PlantView` internals were renamed to match: `slotAt` (ring position) is
separate from `branchForSlot` (which branch that position plays), and the drag
writes the branch that was pressed, not the slot.

### 4. The bottom sliders had no names (missing affordance)

They are the WheelScale, VelSens and Volume macros and they did work; nothing
on screen said so. Each now has a caption above it, a tooltip, and the wheel
readout shows the multiplication the wheel scale slider is doing:
`wheel 1.00 x 0.85 = 0.85 open`. Tooltips come from a `juce::TooltipWindow`
parented to the editor (in a plug-in the parent must be the editor so the tip
scales with the DAW; there is no `setParentComponent`, it is a constructor
argument).

### 5. Branches grew past the circle (real bug)

The recursive drawing had no notion of its own extent: a root of
`radius * (0.10 + 0.72 * d)` with children at 0.48–0.64 of their parent reaches
about 2.7 times the root length, so any branch past half growth left the ring.

Fixed without touching the synth: the tree is grown in **unit space** (root
length 1) into a small vector of `Twig`s, its farthest control point is
measured, and the whole tree is scaled to the growth radius. A quadratic Bézier
lives inside the convex hull of its three points, so bounding those bounds the
stroke. The shape is untouched — only its footprint is normalized — and the
growth target is capped at `kMaxReach` (0.92 of the ring), which is also where
the growth handle sits for the same length: a fully open branch reaches its
handle and nothing ever leaves the circle.

### 6. No separation between seed generation and live control (missing affordance)

`PluginEditor` now has one `layout()` used by both `paint()` and `resized()`,
and `paint()` fills a darker rounded plate (`PanelGray`, corner 10) behind the
two groups of live controls: the macro column (tuning, atonality, effect,
rotation, wheel readout, captioned "live macros") and the bottom strip
(wheel scale, velocity, volume, captioned "output / response"). Seed generation
— the plant, the gene page, new seed / plant branch — stays on the plain
background. The macro plate and its knobs are hidden on the gene page, which
uses the whole body area.

### Verifying UI changes headless

`Xvfb :98 -screen 0 900x700x24 &` then
`DISPLAY=:98 ./build/dev/GenoMosh_artefacts/Release/Standalone/"Dr GenoMosh" &`
then `DISPLAY=:98 import -window root /tmp/shot.png` gives a real screenshot of
the editor. Two caveats: this box has no working fontconfig, so **no text
renders at all** (labels, buttons and knob captions are invisible in the shot —
geometry only), and with no ALSA device the audio callback never runs, so
`currentWheel()` stays 0 and the plant draws at minimum reach. Forcing `len`
and `wheel` to 1.0 temporarily is how the fully grown plant above was checked.

---

## 8. Addendum 2026-10-21 — "the note changes pitch when I play it" (real bug)

Reported again after the atonality fix, from the computer keyboard and from
clicking branches: a note dives (or rises) when it is struck, in a direction
that seems random. Atonality could not touch it, because it was never the
tuning genes.

### Cause

`Voice::updateControlTargets` put the mod envelope straight onto oscillator
pitch:

```cpp
const float pitchModA = p.aMod * modMix * 0.75f + lfoPitch * 0.5f;
```

`modMix` for the default mod source is `modEnv * 2 - 1`, i.e. it starts at −1,
walks to +1 over `mod_atk` and back down as `mod_dcy` decays. `a_mod` / `b_mod`
are bipolar and drawn in ±0.7, so a struck note swept up to **two thirds of an
octave**, upward if the gene was positive and downward if it was negative, over
the first few hundred milliseconds. Every key is a different branch with a
different sign, hence "random".

This is what 5.3 rules out: *"Pitch is played as normal. The branch changes
timbre and modulation, not the played pitch, except through the oscillator
offset genes."* Section 6's "the pitch slide is not a bug" was wrong about that.

### Fix

The mod envelope has no destination in pitch any more. Pitch is the note, times
the `a_freq` / `b_freq` offset (through the ratio snapper), plus LFO vibrato
(`lfo_amt` / `lfo_bal`) and a few cents of `drift_amt` — the two things the
document does ask for under "voice-level life".

`a_mod` / `b_mod` keep their name and their velocity scaling and move to the
destination they describe: each oscillator's own spectrum (`colour`, i.e. pulse
width and added partials), smoothed at control rate like every other modulation
target:

```cpp
constexpr float kModColourDepth = 0.35f;
colourATo = clampf (p.aColour + p.aMod * modMix * kModColourDepth, 0.0f, 1.0f);
```

So the mod envelope, the loop envelope and the LFO still open and close the
timbre while a note is held — the movement the project wants — they just do it
in tune. If a real pitch envelope is ever wanted it should be its own gene, not
a side effect of timbre modulation.

### Measured (`testPitchStability`, new, runs second in the harness)

A genome with both oscillators tuned to the pressed note, no vibrato, no drift,
no detune, and a deliberately large `a_mod` / `b_mod`; note 60 rendered on all
twelve branches at distance 0.85; the period is found by autocorrelation in a
window 60 ms after the attack and another 550 ms after it.

```
before:  branch 0  period  50 ->  68   160.2 cents   (and not the pressed note)
         branch 1  period 136 -> 243   302.5 cents
         ... every branch fails, up to 3 semitones of slide
after:   [PASS] a held note keeps the pitch it was pressed at
         worst pitch movement while held: 2.8 cents
```

The 2.8 cents is the per voice random detune plus one sample of autocorrelation
quantization at a 183 sample period, i.e. nothing.

---

## 9. Addendum 2026-10-21 — Atonality becomes a generation parameter; Rotation removed

### Atonality: from a runtime blend to a generation time ceiling

Section 7 fixed the direction of the ratio blend; the operator has since
redefined what the parameter is for. It is now **generation facing**: it never
modulates a note that is already sounding, it bounds what the plant is allowed
to contain.

Atonality is a ceiling on the *potential* of every gene that can move the
played pitch. Those genes are tagged `kFlagPitch` in the gene table:

| gene | neutral (no effect) | at atonality 0 |
| --- | --- | --- |
| `a_freq`, `b_freq` | 0.5 (unison) | unison |
| `lfo_amt` (vibrato) | 0.0 | no vibrato |
| `drift_amt` | 0.0 | no pitch drift |
| `det_spread` (per voice detune) | 0.0 | no detune |

`applyAtonality` (Genetic/Mutation.cpp) clamps each of them to
`neutral ± atonality * span`, and for the two oscillator offsets it then puts
the value on the ratio ladder (`snapToRatio`, 5.5) — so a plant is harmonically
related to the key that plays it at any setting, and atonality decides how far
up the ladder it may reach.

It is a **ceiling, not a multiplier**, and that is deliberate: it is idempotent,
so planting a branch (which feeds a resolved genome back in as the next seed)
cannot ratchet the tuning down a little further every time. `randomizeGenome`
draws pitch capable genes *inside* the band rather than clipping them into it,
so a low atonality seed still has a spread of tunings instead of a pile on the
ceiling.

Applied in three places, all of them generation time:

1. `randomizeGenome` — the draw is inside the band;
2. `buildBranchDirection` — the pitch components of a branch direction are
   scaled by it, and `buildPlant` gates the seed the audio thread sees;
3. `resolveBranchGenome` — the resolved genome is gated again, because the sum
   of seed plus displacement is not automatically inside the band.

`MacroValues` no longer carries `atonality`, and `resolveVoiceParams` no longer
calls `snapToRatio`: the voice just maps the gene. The parameter is still a
normal host automatable float, but the processor listens for it
(`AudioProcessorValueTreeState::Listener::parameterChanged`) and answers by
rebuilding the plant. Default raised 0.25 → **0.50**: half the range of those
genes is available, which is octaves and fifths and a real vibrato, and it is
what makes a random plant sound related to the keyboard.

Measured (`testAtonalityCeiling`, new, runs second):

```
atonality 0.00 -> furthest pitch gene at 0.000 of its range
atonality 0.25 -> furthest pitch gene at 0.177 of its range
atonality 1.00 -> furthest pitch gene at 0.710 of its range
[PASS] atonality bounds every pitch capable gene at generation time
```

(0.710 at atonality 1 is the soft ranges doing their job, i.e. the full range is
available.) `testPitchStability` now runs with `mp.atonality = 0`, which makes
the "a held note keeps the pitch it was pressed at" check airtight: 2.8 cents
worst case, all of it the per voice detune.

One consequence worth knowing: `resolveBranchGenome` at distance 0 returns the
**plant's** seed, which is the gated copy `buildPlant` stored, not the raw
genome the gene page edits. The gene page shows the seed as drawn; atonality
decides how much of it is realised. The 5.1.3 test was updated to compare
against `plant.seed` for that reason.

### Rotation removed

The operator's verdict: useless in practice. Gone — the parameter, the macro,
the knob, and the plant view's ring/plant offset. `branchForNote(midiNote)` is
now just the pitch class, and a ring slot plays the branch of its own pitch
class, which is the same thing, so the view and the audio side still agree by
construction.

### Layout

The macro column is split by what the controls actually are:

* top row, on the plain background: **atonality** (generation facing, it
  rebuilds the plant rather than riding a live sound). The left half of that row
  is deliberately empty.
* bottom row, on the darker plate: **effect** and **tuning** — the two that are
  safe to move while playing. The "live macros" caption moved onto that plate.
* the bottom strip plate (wheel scale / velocity / volume) is unchanged.

`Layout` in `PluginEditor` now carries `knobRowTop`, `knobRowBottom` and
`macroPlate` instead of one column rect.

## 10. Addendum 2026-10-30 — the envelope becomes a pluck; the modulator follows it

> **Superseded in part by §11 (2026-10-31):** `vol_sus` is gone, `vol_fade` and
> `vol_floor` exist, the outer curve no longer settles on a sustain level, and
> `env_time` is a ceiling rather than the fade itself. The shape of the pluck and
> everything in "Modulation" below are unchanged.

The Rev 3 / Rev 4 work was reverted (it stopped sounding organic and had audible
bugs), but two parts of it were wanted: the envelope, and the modulation. This
addendum is what landed on top of the reverted code. **The genetic engine was
not touched**: `GeneTable` (43 genes, ranges, curves, sensitivities, soft
ranges), `Mutation` (directions, distance response, traits, couplings, safety
score, atonality, loudness compensation), `PlantEngine`, growth and the UI all
still behave exactly as before, so a seed still grows the same plants and the
same kind of variety.

### What the envelope is now

`Source/Engine/Dsp.h`: `Env` (the smoothed ADSR) is gone. `SharedEnv` is one
clock and two curves multiplied:

```
amp(t) = pluck (phase)  ×  outer (t / duration)  ×  gate
mod(t) = pluckM(phase)  ×  outer (t / duration)  ×  gate
```

* the **pluck** starts at zero, rises to a single peak, falls back to zero. It
  never plateaus, and it repeats at `period`. Its fall is a fixed curve
  (`kPluckFallExponent = 0.6`: fast drop, long tail) — a gene is allowed to bend
  the *rise*, never the fall, because a gene that can flatten the fall is what
  turned the pluck into a straight ramp with a linear window.
* the **outer** curve is a level applied to the whole train of plucks: 1.0 →
  `vol_sus` over `env_time`, bent by `vol_dcy`. `env_time ≥ 0.9` means *no
  outer decay at all* — endless plucks until the key is released. Nothing to
  settle to (`vol_sus ≈ 0`) means the note ends by itself and frees its voice.
* **note off** is a fade from wherever the level currently is (`vol_rel`), so a
  voice can be released in the middle of a pluck without clicking.
* both curves are evaluated through a 512 entry wavetable built at note on, so
  the sample loop does no `pow()` and a parameter can never zipper a held note.

`Voice` holds one `SharedEnv` instead of three envelopes (`ampEnv`, `modEnv`,
`loopEnvOneShot`), and the voice stealing meter (`ampLevel()`) became a slow
peak follower: with a pluck envelope a voice is at zero twice per cycle, and it
must not be stolen for being "quiet" between two of its own plucks.

### What the genes mean now (mapping layer only, `VoiceParams.cpp`)

| gene | before | now |
| --- | --- | --- |
| `env_time` | loop period / shape time | the level decay over the repeats; ≥0.9 = none |
| `env_loop` | 0..1 "loop amount" | the period of one pluck, 30 ms … 16 s |
| `env_tilt` | triangle duty | where the peak sits in the cycle (neutral = 6 % = a plain pluck, top = swell) |
| `vol_atk` | attack time | curvature of the pluck's rise (0.5 = straight) |
| `vol_dcy` | decay time | how the outer fade bends (high = holds, then collapses) |
| `vol_sus` | sustain level | the floor the repeats settle at (unchanged value semantics) |
| `vol_rel` | release time | unchanged |
| `mod_atk` / `mod_dcy` | mod ADSR times | where the modulator's peak sits relative to the level's, and how much of the level's fall window it uses |
| `mod_sh` | exponent on the mod curve | sample and hold on the modulator, off until 0.75, 8 Hz → 0.1 Hz |
| `a_mod` / `b_mod` | oscillator *timbre* amount | bounded **pitch** modulation per pluck, hard capped at ±12 st in the mapping layer |
| `lfo_bal` | pitch / filter split | equal power **vibrato / tremolo** split; the LFO is free (never envelope scaled) |

Two things worth flagging, both deliberate:

1. **The table still documents the old units.** `vol_atk`, `vol_dcy`,
   `mod_atk`, `mod_dcy` are `Exp` seconds and `env_loop` is a `Linear` amount in
   `GeneTable.h`, and only the mapping layer interprets them as curvature and as
   a period. Every gene's *direction* is preserved (a plant that was punchy is
   still punchy), which is why the same seeds stay recognisable. Making the table
   metadata honest is a separate change to `curve/min/max` only — sensitivities
   and soft ranges would not move, so it is still a genetics decision and was
   left to the operator.
2. **`vol_sus` is *not* gated at 0.5** the way the failed Rev 3 design gated it.
   A plant's sustain is used as the floor directly. That is a large part of why
   this did not reproduce the failed design's level collapse.

### Modulation

`modSource` stays a structural trait, and its three values still mean "this
branch modulates from something else", but the sources are the new ones: 0 = the
pluck's own curve (the default), 1 = the outer curve only (a slow modulator that
follows the note rather than each pluck), 2 = the LFO. Velocity scales the
modulator once (`VelSens × mod_vel`), `mod_sh` re-quantises it and is *not*
smoothed (the step is the sound). Destinations: pitch (`a_mod` / `b_mod`,
bounded), filter cutoff (`flt_mod`), FM index (`fm_mod`). The LFO adds vibrato
and tremolo at one rate for the whole note, and the drift generators are
unchanged. The old `a_mod` → oscillator colour route is gone; timbre movement
comes from the filter, the FM and the pluck itself.

### Measured

`GenoMosh_Tests`: **56 checks, 0 failures** (was 20). `cmake --build
cmake-build-debug` builds VST3 + Standalone + tests, no new warnings.

New tests, all of them asserting a property rather than a snapshot:

* `testEnvelopeMapping` (17 checks) — the mapping layer: no outer decay at the
  top of `env_time`, period monotone in `env_loop` and never longer than the
  note, peak position monotone in `env_tilt` and a plain pluck at neutral,
  `vol_sus` as the floor, straight curves at 0.5, the modulator's peak inside
  the cycle, the `mod_sh` gate, pitch amounts bounded at an octave and zero at
  neutral, the equal power vibrato/tremolo split.
* `testPluckEnvelope` (13 checks) — the block: peaks where tilt says, reaches
  full level, never plateaus, returns to zero, repeats identically, the outer
  curve lands exactly on the floor and holds, `vol_dcy` bends it, a note with no
  floor ends by itself, note off fades without a click (largest sample to sample
  step < 0.02), and the modulator can finish while the level is still up.
* `testModulationReachesAudio` (5 checks) — pitch measured off real audio by
  interpolated upward zero crossings (an autocorrelation cannot read a pulsed
  level): 1 cent of excursion with `a_mod` neutral, 910 cents with it open, never
  past the octave; the tremolo half moves the level of a held note
  (0.060 … 0.075 relative wiggle at ~2 Hz) and does not change its mean level
  (+2.6 dB).

Level table (`testRender`, 12 branches at distance 0.85, 48 kHz, seed
`render-seed`, before → after): peaks 0.44…0.58 → 0.45…0.54, rms 0.064…0.087 →
0.055…0.070, DC ≤ 5e-4 everywhere, every branch still audible, all twelve still
inside 12 dB of each other.

Peaks are unchanged; the average level came down ~1.3 dB. No makeup gain was
added — the pluck's duty (0.62 for a neutral shape) is *higher* than the old
ADSR's, so the difference is only the outer decay doing its job. The first
attempt at this did collapse the level by 6 dB: that was slow `env_loop` values
leaving the voice rising for longer than the note, and it is fixed by the rule
the reference itself states — **a pluck longer than the note is one pluck
spanning the note** (`period = min (period, duration)`), which is asserted in
`testEnvelopeMapping`.

### Known holes left open on purpose

* `testPitchStability` keeps its assertion but its seed now sets `a_mod` /
  `b_mod` to neutral, so it measures "nothing *else* moves the pitch". What the
  modulator does when it is open is `testModulationReachesAudio`.
* `a_mod` / `b_mod` are not `kFlagPitch`, so atonality does not converge them.
  The octave ceiling in the mapping layer is what keeps that playable. Adding the
  flag is a one line genetics change for the operator to make or refuse.
* The LFO's waveform is still random per voice (sine / triangle / sample and
  hold). The failed design forced a sine; nothing here asked for that, and the
  random shape is part of the current variety — but it is why the tremolo probe
  asserts "moves the level, keeps the mean" rather than "vibrato leaves the level
  completely alone".

---

## 11. Addendum 2026-10-31 — Amp Sustain becomes Amp Floor, and Amp Fade counts the repeats

The report was: *"I don't like how the current sustain settles the pluck to a level
above 0 while the pluck is still audible."* That is exactly what a **floor on the
outer curve** does: the train of plucks keeps its shape and gets a level under it,
so a "sustained" plant was still a pluck, only louder in the gaps. It cannot make a
sound that *isn't* plucked. Two genes replace it:

| gene | label | means |
| --- | --- | --- |
| `vol_floor` | Amp Floor | how low the amp may go **between repeats**. 0 closes all the way to silence (the plain pluck), 1 is a straight level with no pluck left in it. In between the level keeps moving without ever closing. |
| `vol_fade` | Amp Fade | how long the fade to silence takes, counted in **repeats**: 0 is one single pluck, 0.5 is a train of them, the top of the range never fades at all. |

`vol_sus` (Amp Sustain) and the outer `FloorLevel` are deleted. A level that
settles above silence and stays there forever no longer exists as a thing: either
the fade ends the note, or the note runs until the key is released. A sustained
sound is `vol_floor = 1`, and it still fades out unless `vol_fade` says otherwise —
which is the operator's two examples, and both are asserted in the harness:

* `vol_floor 1, vol_fade 0.5` → a flat level that fades out by itself.
* `vol_floor 1, vol_fade 1` → a flat level that holds until key up.

### The model (`Dsp.h`)

```
amp(t) = (ampFloor + (1 − ampFloor) · pluck(phase)) × outer(t / fadeDuration) × gate
mod(t) =  pluck(phase)                              × outer(t / fadeDuration) × gate
```

* The floor **lifts the bottom of the pluck curve, never its peak**: the peaks stay
  at unity, so a floored note is the same sound left audible rather than a different
  one. It is baked into the level wavetable in `setShape()` (`buildTable` now takes
  the floor it should bake in), so it costs nothing in the sample loop and cannot
  zipper a held note.
* **The modulator is deliberately not floored.** A level that stopped moving is
  still modulated once per repeat — pitch dive, filter and FM — and that is what
  keeps a pad here alive instead of dead. Asserted directly.
* **The outer curve always ends at silence**, bent by `vol_dcy` as before, so the
  self termination in `process()` is unconditional now: a note whose fade is over
  ends on the exact zero and frees its voice, click free.

### The mapping (`VoiceParams.cpp`)

* `vol_fade` counts repeats, which is what makes a fade the same length *in music*
  at any pluck period: `cycles = expRange(1, 64, gene) × period`, with
  `gene ≥ 0.95` meaning no fade. Counting was the operator's spec ("fade 0 → only
  one pluck, fade 0.5 → several"), and it is why the gene is `Linear` in the table
  and exponential only here — same convention as `env_loop`.
* `env_time` was the fade's length in seconds; two genes setting one thing is a
  conflict, so it becomes an **absolute ceiling** on the same fade (a slow pluck
  counted in repeats could otherwise run the voice for minutes). Its existing
  "top of the range = none" convention is kept (`env_time ≥ 0.9` = no ceiling), so
  `Duration = min(cycles × period, ceiling)`, and no ceiling at all on either side
  is handed to the block as `Duration = 0` ("no fade") exactly as before.
* `vol_floor` is a straight `clamp01` of the gene: the top of the range *is* the
  flat level, so the mapping layer adds no ceiling of its own. Soft range
  `0.00 … 0.65`: random seeds sit articulate, and a plant can still be grown into a
  pad.

### The genetic engine (`GeneTable.h`, `Mutation.cpp`)

* 44 genes (was 43): `vol_floor` and `vol_fade` take `vol_sus`'s place in the
  `GrpAmp` block. Both `sens = 0.45` — these are now the two genes that decide
  whether a plant is a pluck or a pad, so they should move.
* The "never close everything off" guard rail and the silence term of `safetyScore`
  now read `max(vol_floor, vol_fade)`: with the new model the only way a branch is
  inaudible is one single very short pluck, and either gene fixes it.
* `randomizeGenome` biases `vol_fade ≥ 0.30` (a note that lasts long enough to
  hear) and deliberately **does not bias `vol_floor`**: plucky vs sustained is the
  variety the player grows for.
* `loudnessCompensation` gained a duty cycle term (`1 + 2.2·floor + 0.5·fade` under
  the `1/sqrt`). A level that never closes carries several times the energy of one
  that does; without this a pad branch sits ~8 dB above a plucky branch at the same
growth, which the player hears as a bug rather than as a feature.

### Measured

`GenoMosh_Tests`: **68 checks, 0 failures** (was 56). New: `testAmpFloorAndFade`
(7 checks: no floor closes to silence, a mid floor lifts only the gaps and keeps the
peaks at full level, floor 1 is a straight line, the modulator still moves at floor
1, a floored level fades out anyway, no fade holds until key up, note off on a held
level does not click), plus fade/floor assertions in `testEnvelopeMapping` (one
pluck at `vol_fade 0`, monotone, several repeats at 0.5, `env_time` as the ceiling,
floor mapping at 0 / mid / 1) and re written outer curve checks in
`testPluckEnvelope` (fades to silence rather than settling, ends by itself).

Level table (`testRender`, 12 branches at 0.85, seed `render-seed`) before → after:
peaks 0.45…0.54 → **0.63…1.03**, rms 0.055…0.070 → **0.096…0.219**. That is +5 dB
of average level, and it is the point of the change: these plants sound most of the
time now instead of between plucks. The compensation above is what keeps the twelve
branches within ~4 dB of each other; **no master trim was added** — one branch
peaks just over 1.0, so if that bothers the operator the honest place to fix it is
the default `MacroValues::volume` or a trim on the master, not more gene surgery.

Two things to know:

1. **Old patches.** The seed is stored by gene id, so a pre change state still
   loads; `version` is bumped to 2 to record the break. A gene the state never had
   now gets the middle of its soft range instead of 0, so an old patch does not come
   back as one single click.
2. `Failed_Design.md` also lists a `vol_fade` (a gate above 0.75 that pulls the
   sustain floor to silence). Same id, unrelated meaning — that document is a record
   of the failed Rev 3 design and was not edited.

### Still open

* `env_time` and `vol_fade` both describe the note's length, one in seconds and one
  in repeats. The ceiling is defensible (it bounds a slow pluck), but it is the one
  remaining pair of genes that fight over a single job. If it turns out to be
  confusing in the grid, `env_time` is the gene to remove or repurpose.
* No expression or aftertouch; `vol_floor` is a gene, so it can only be changed by
  re planting or in the grid, not ridden live from a controller.

---

## 12. Addendum 2026-11-02 — the filter is anchored to the pitch being played

The report was: *"certain keys of a given seed sound VASTLY different from the
other notes"* — and, after the branch lengths were pulled to 0 and then to 0.25,
*it stayed*. That second half is the whole story: at length 0 `applyDistance`
returns the seed verbatim and `resolveTraits` returns all defaults, so **no plant
parameter can be responsible**. One seed, one genome, and one key sounding like a
hi-hat while its neighbours are plucks.

### Cause

The genome is relative (everything is `[0,1]`, then octaves and ratios); the voice
was absolute. `midiNote` reached the sound in exactly two places — `noteToHz()` and
the `flt_kf` term — so every other part of the voice ran at the same Hz whatever
key was pressed. A typical random plant puts the cutoff at 340…1600 Hz with
`flt_q` 0.3…0.6 (Q ≈ 4…8) and a second stage up to two octaves away: a **fixed pair
of formants sitting on the keyboard**. The one key whose fundamental crosses them
loses its body entirely — what is left is the top of its own spectrum, the noise
mix (which never changes with pitch), and the filter's ring, all cut by a short
pluck: a hi-hat. `flt_kf` could have tracked the keyboard, but it is a mutated gene
drawn anywhere in ±1 octave/octave and neutral 0 means *no tracking at all*, so
most plants had nothing following the key and the top of the keyboard outran the
filter by several octaves.

This is the gap `Failed_Design.md` named twice and the phase 3 acceptance criterion
asked for: *"cutoff locked to A's pitch + flt_sep + flt_q + flt_kf (soft-railed)"*,
*"`a_freq` links B's pitch and the filter cutoffs: resolve that once in
`VoiceParams`, so every branch tracks the keyboard"*, *"`flt_freq` tracks the
keyboard"*.

### Fix

No gene was added, renamed or re-ranged, and no preset state version was bumped.
One anchor was added, in the mapping layer, where the design put it:

* `resolveVoiceParams` now resolves **the voice's pitch once**: `noteFrequencyHz`
  (the key plus the tuning macro) and `oscillatorAFrequencyHz` /
  `oscillatorBFrequencyHz` (that, with `a_freq` / `b_freq` on top). `Voice::start`
  reads these and only clamps them for audio — computing the pitch twice is how a
  cutoff ends up an octave away from the pitch it was meant to follow.
* `filterHz = oscillatorAFrequencyHz × (flt_freq's Hz / noteToHz(kFilterReferenceNote))`,
  with `kFilterReferenceNote = 60`. The gene keeps its whole meaning **at middle C
  for a voice at unison** — nothing a plant author set at C3 moves — and from there
  the cutoff is one octave per octave with the key, locked to oscillator A rather
  than to the bare key, so a branch that offsets A keeps its spectral relationship
  too. Unclamped at this layer on purpose: it has no sample rate, and the voice
  already clamps.
* `flt_kf` is re-semantified as the **deviation from locked tracking**: neutral
  tracks exactly (it is now a gene whose neutral point really is the resting state),
  and either end asks for an octave per octave either way on top, i.e. 0…2 octaves
  per octave of absolute tracking. `Voice::updateControlTargets` is unchanged — it
  was already adding octaves of deviation.
* **The old behaviour is one gene end away, not lost.** `flt_kf` at its lowest
  (−1 octave per octave) cancels the tracking exactly, which is the classic analog
  posture: a filter held at fixed Hz that the keyboard plays through, high notes
  dark and low notes open. It is asserted, because that cancellation is arithmetic
  the mapping layer is now responsible for.
* The `flt_freq` comment in `GeneTable.h` says which ranges in the table are not
  absolute, and the resonance-safety coupling in `applyCouplings` now reads as what
  it is: less resonance the higher the cutoff sits *above the note it is filtering*
  (the normalized value is that height at every key, which makes the guard rail
  more correct than it was, not less).

### Measured

`GenoMosh_Tests`: **77 checks, 0 failures** (was 68). New: `testFilterTracksKeyboard`
(9 checks). The numbers it prints, before → after the change:

```
before:  cutoff at note 48 383.5 Hz, note 60 383.5 Hz, note 72 383.5 Hz
         rms at note 60 0.09885, note 72 0.02854  (-10.8 dB)     4 checks FAIL
after:   cutoff at note 48 191.7 Hz, note 60 383.5 Hz, note 72 767.0 Hz
         rms at note 60 0.09885, note 72 0.10005  ( +0.1 dB)     all PASS
```

The audio half is the honest one: a plain held sine, one closed filter
(`flt_freq 0.37` → 378 Hz), `flt_q`/`flt_mod` at rest, `flt_kf` neutral, and the
same seed rendered at two notes an octave apart. An untracked filter lost 10.8 dB
of the high note; anchored, the two are within 0.1 dB. The mapping half asserts the
tracking is exact (one octave per octave), that the gene still means its own value
at the reference note, that a neutral `flt_kf` asks for no deviation, and that the
anchor is oscillator A's pitch rather than the key (a `+12 st` `a_freq` moves the
cutoff an octave with it). `testPitchStability`, `testRender` (levels, reproducibility,
three sample rates) and everything else are unchanged and still pass; every
translation unit in the project, harness included, syntax-checks clean from
`compile_commands.json`.

What a player hears: a plant is one instrument across the keyboard instead of one
instrument per register. Hot and cold spots from resonance still exist, but they
now travel *with* the key instead of being pinned to a frequency, so they no longer
single out one note.

### Still open

Anchoring the filter was the largest one, not the only one. The same class of bug —
a quantity that should be relative to the note being absolute — is still live in:

* **FM and hard sync**: `fm_amt` is a phase index, so sidebands sit at the note's
  own frequency; `nextModulated` band-limits nothing and the sync at
  `Voice.cpp:277` resets an audible oscillator's phase with no PolyBLEP. Both are
  mild in the bass and noisy at the top of the keyboard.
* **`env_loop` in seconds**: a 30 ms pluck holds under four cycles of a note below
  ~130 Hz, so the low register articulates as ticks however the amp genes are set.
  (`env_kf` — key follow of the cycle length — is the gene for this and does not
  exist yet; `Failed_Design.md` lists it.)
* **The sub at exactly −1 octave and the master `softClip`**: subsonic level on the
  lowest keys is summed at a fixed gain and drives a memoryless saturator, so only
  the bass distorts. There is still no per-key level trim anywhere —
  `loudnessCompensation` is a function of the genome alone.
* **Per-note life is seeded per note**, including a random LFO waveform
  (`Voice.cpp:86`: sine / triangle / sample-and-hold). It is not a per-*key*
  difference, but it is a per-note one, and a sample-and-hold LFO at `lfo_amt` up
  to an octave of vibrato is a loud one. Whether that draw should be per note, per
  branch or per plant is an open design question (§11's list still applies).

---

## 13. Addendum 2026-11-02 — the editor becomes a web page

The UI is now HTML/CSS/JS (`WebUI/`, embedded in the binary and served by a
`juce::WebBrowserComponent` resource provider). `Source/UI/PlantView.*` and
`Source/UI/GeneGrid.*` are deleted; the ring, the growth handles, the gene
table, the knobs and the computer keyboard all exist in the page. Everything
else is unchanged: the engine, the gene table, the parameter set, the state
format.

What moved and what did not:

* **Shape stays authoritative in C++.** `Source/Web/PlantGeometry.cpp` is the
  old `PlantView::growTree`, unchanged, emitting unit-space strokes plus their
  measured reach. `WebUI/plant.js` only deforms and scales what it is given, so
  "grow the tree in unit space, measure, then scale to `kMaxReach`" and "the
  ring never moves" are still true, and the plant on screen cannot disagree with
  the plant the audio thread resolves.
* **New in this UI: the plant is animated.** Wind (three incommensurate sines,
  per-branch phase, bend growing with radius), a damped spring on grab/drag/
  release, a glow plus extra sway fed by a note, and growth that is followed
  instead of jumped to. All of it lives in `plant.js`.
* **Ranges still exist once.** The page builds its controls from `ui.controls`,
  generated from the APVTS; the keyboard map still exists once, in
  `ComputerKeyboard` (the page forwards typed characters).

Measured (Release build, Xvfb, `WEBKIT_DISABLE_COMPOSITING_MODE=1`): the page
loads from the binary and renders (header, plant, knobs with real values,
sliders, gene page with group colours); readouts show the real defaults (0.85 /
0.60 / 0.75) rather than the page's placeholders; two screenshots one second
apart differ in 219 px of the plant area, which is the wind; `ctest` 1/1 green;
VST3 + Standalone build warning-free.

Two traps recorded in `Docs/Web-UI.md`, which is the reference for this area:
`juce::var` owns the `DynamicObject` (wrapping one pointer twice frees it in the
first temporary — it crashes inside `JSON::writeToStream`), and anything emitted
before the page has installed its listeners is silently lost, so the back end
waits for `ui.ready` and resets its change detection there.

`Docs/Codebase-Map.md` sections `UI/PlantView` and `UI/GeneGrid` describe code
that no longer exists; `Docs/Web-UI.md` replaces them.
