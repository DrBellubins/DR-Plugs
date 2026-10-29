# GenoMosh — Implementation Status

*Report format requested by the design document, section 14: what was built,
what was assumed, measured results, open questions.*

*Planned changes against design document Revision 2 (48 genes, the shared looping
envelope engine, LFO to pitch/volume, `adj_*` output stage): see
**`Docs/Rev2-Alignment-Plan.md`**. Everything below section 9 describes the code as it is
now, which is Rev 1's gene set (43 genes).*

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
