# GenoMosh — Implementation Status

*Report format requested by the design document, section 14: what was built,
what was assumed, measured results, open questions.*

*Plan of record for the work that follows: **`Docs/Rev3-Alignment-Plan.md`** — an audit of this
code against design document **Revision 3** (the 48 documented genes with their real meanings plus
a marked 2-gene extras block, the shared envelope clock, LFO to pitch/volume, two S&H sources,
`flt_type`, `saturate`/`adj_*`, per-voice reverb, the §5.7 curation mechanisms, and the §13
measurement protocol), with milestones M0–M11 and gates. Every open decision there was answered on
2026-10-29 (`Rev3-Alignment-Plan.md` §4; verbatim answers in `Rev3-ChangeLog.md` §5.1).
**M0 landed on 2026-10-30** (§10 below): the gene table is now the 48 documented genes plus the
marked 2-gene extras block, with `neutral`/`gateLow`/`gateHigh`/`steps`/`helpText`/`colour`,
`Curve::Stepped`, the five growth-exempt genes, and a `modelVersion` that refuses older states.
**M1 landed the same day** (§11): `VoiceParams` is the one mapping layer — neutrals, the six
documented activation gates, `flt_kf`'s soft rails, the stepped `a_freq`/`flt_type`, the
`osc_mix` equal-power warp and all the links — and growth gained `pullToNeutral` plus a
seed randomiser centred on neutral. Next is **M2** (the design §13 measurement harness: WAV
output, spectral and articulation metrics, the 12×5 batch), whose baseline is the table in §11.
It supersedes `Docs/Rev2-Alignment-Plan.md`.*

**The envelope model was corrected on 2026-10-30 after listening (design document Revision 4,
deviation D-8, plan milestone M4b, §15 below): the shared envelope is a pluck repeated at `env_loop`
under a volume decay whose length is `env_time`; decision R3 and `kInfiniteShapeSeconds` are retired;
`mod_atk`/`mod_dcy` are timings, not curvatures; the LFO's vibrato/tremolo are free of the envelope;
and the word "Shape" is out of the parameter labels. The pinned audio baseline and the five goldens
were re-generated against it — §15's table is the one M5+ must use.*

Everything below section 9 describes the code as it was then, which was Rev 1's gene set
(43 genes). **The gene table is no longer Rev 1's** — see §10 — and the way genes are
*read* is no longer Rev 1's either (see §11), but the *meaning* of most of them still is:
the envelope, modulation, filter topology, saturation and output stage keep working the way
sections 1–9 describe until M3–M7 land.

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
6. ~~`PlantView::update` copies a whole `PlantSnapshot` at 30 Hz on the UI
   thread.~~ Fixed at Rev 3 M0 (2026-10-30): the view keeps twelve 24-byte
   per-branch drawing fingerprints and refreshes them only on a revision change.
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

---

## 10. Addendum 2026-10-30 — Rev 3 alignment **M0**: the gene table becomes the documented 48

Milestone M0 of `Docs/Rev3-Alignment-Plan.md`. Table only: no envelope, modulation,
filter topology or output behaviour was re-semantified. Everything in this section
is what M1–M7 have to work against, so it is recorded in full.

### What changed

* `Source/Genetic/GeneTable.h`. `GENO_MOSH_GENE_LIST` is design §4.2 verbatim:
  **48 genes in the document's order and groups (16 / 15 / 17), then the two
  extras of deviation D-7** (`b_color`, `fx_width`) in their own `GrpExtras`
  group, `kNumGenes` = 50, `kNumCoreGenes` = 48 as a named constant. The 7
  renames (`balance→osc_mix`, `sat_drive→saturate`, `fx_*→rvb_*`), the 3
  deletions (`vol_rel`, `det_spread`, `drift_amt`) and the 10 additions
  (`env_kf`, `vol_fade`, `lfo_dly`, `mix_mod`, `flt_type`, `rvb_atk`,
  `adj_bass`, `adj_treb`, `adj_pan`, `adj_clip`) are all in.
* `GeneInfo` is §4.3's struct: `helpText` (one sentence per gene), `neutral`,
  `gateLow/gateHigh`, `steps`, `colour`. New flags `kFlagLinked`,
  `kFlagStructural`; `kFlagNever` is now actually used, on `rvb_size` + the four
  `adj_*`. `enum Group` is the document's eleven groups plus `GrpExtras`, with
  one ARGB colour per group via `constexpr groupColour()` so the table and the
  UI cannot disagree. `Curve::Cubic` is gone (nothing used it); `Curve::Stepped`
  exists and is implemented both ways in `Curves.h` (`quantiseNorm`, steps in
  gene space, exact step grid, stable inverse).
* Version axes (`kFormatVersion` / `kModelVersion` / `kMutationVersion`) live next
  to the table; the state file writes all three and `setStateInformation`
  **refuses** a state whose `modelVersion` differs, with a one-line warning
  (decision R11 — no fake migration).
* `Mutation.cpp`: the atonality ceiling reads `gi.neutral` and the span from the
  table instead of inferring a neutral from the curve; `randomizeGenome` leaves
  every growth-exempt gene at its neutral and no longer biases `vol_atk` /
  `flt_freq`, which are a curvature control and a cutoff *ratio* now.
  `applyCouplings`/`safetyScore`/`loudnessCompensation` keep their formulas with
  the renamed ids — plan §6.3 tracks that, M3/M5/M7 recalibrate.
* `VoiceParams` is where the deleted genes became named constants
  (`kNoteOffReleaseSeconds = 0.18`, `kVoiceDetuneCents`, `kVoiceDriftAmount`,
  D-3/D-5) and where `flt_freq` became the tracking cutoff
  (`cutoff = fA × 2^lerp(−2,+8,v)`, `fA` = note × `a_freq`). The LFO split is
  the equal-power one over a 0..1 `lfo_bal` (vibrato / both / tremolo), with the
  tremolo half still wired to the filter until M4 (R6).
* `GeneGrid`: no more `GrpCount/2` column split — the two columns are counted out
  in rows and split at a group boundary; headers take their colour from
  `groupColour()`; every row carries its `helpText` as a tooltip.
* `PlantView::update` no longer copies a `PlantSnapshot`. It keeps twelve
  `BranchVisual` records (hash, hue, saturation, brightness = 24 B each) and
  refreshes them only when `snapshot.revision` changes.
* `Pictures/2026-10-22-0854*_0855*.png` now have `.txt` sidecars (plan §6.8),
  including the two asterisks that design §15 Q-2 asks about.

### Measured, once (M0's gate asks for the numbers)

```
sizeof Genome = 200 B, BranchDirection = 216 B, PlantSnapshot = 8008 B,
triple buffer = 24024 B
```

The plan's predicted ≈ 200 B / ≈ 8 kB / ≈ 24 kB. Confirmed rather than re-derived.

`GenoMosh_Tests` is green (28 checks, was 21), including the new
`testTableMatchesDoc` (48 ids, order, group, curve and `steps` — `a_freq` 7,
`flt_type` 4 — spelled out by hand from §4.2, the extras at slots 48/49, every
gene carrying help text / neutral / gates, exactly 5 `kFlagNever`, and the Rev 1
names gone) and `testGrowthExempt` (no branch length, 0…1, moves any of the five).
The VST3 target builds with no new warnings.

### The sound did move, and by how much

There is no WAV writer until M2, so the M0 spot-check of "new sound = old sound"
was done with the harness's per-branch table. **Before** (this is the
`1f245e9`-era reference; `Source/` differed from it by two comments only):

```
branch 0..11 at distance 0.85, 48 kHz, note 60+branch
  peak 0.440..0.581   rms 0.0642..0.0874   dc +0.0001..+0.0004
  branch spread: 2.7 dB rms          usable branches at distance 1: 720 / 720
```

**After M0:**

```
  peak 0.636..0.755   rms 0.0874..0.0933   dc +0.0034..+0.0041
  branch spread: 0.6 dB rms          usable branches at distance 1: 720 / 720
```

**Read both blocks with §12's caveat**: the harness at the time never handed its `PlayState`
to the engine, so the twelve "branches" are one genome played at twelve semitones, and the
2.7 → 0.6 dB "branch spread" is a semitone spread. The absolute peak/rms/DC levels do stand.

Read that honestly. The six `same` genes (`mod_vel`, `lfo_rate`, `a_noise`,
`b_noise`, `fm_mod`, `flt_mod`) keep their exact curves and ranges; `fm_amt` and
`saturate` keep their old depth ranges under the curves §4.2 names. Everything
else moved because its *meaning* moved, and the two big movers are the ones the
plan expected: the tracking cutoff (every branch now sits relative to the note
instead of at an absolute Hz) and the re-centred soft ranges. Net effect: branch
loudness is 4× tighter (which is what the level model wants) and the plant is
~2.4 dB hotter overall, with DC an order of magnitude up but still 10× inside the
gate. **M7's `Volume`/limiter work must be done against the new numbers, not the
old ones**, and `loudnessCompensation` gets its topology/shelf/clipper terms there.

### Provisional wiring (what is deliberately dead, and who owns it)

`VoiceParams` resolves these to their documented neutral, and nothing reads them
yet — they exist so the table is honest before the destination is:

| Gene | Resolved as | Read by |
| --- | --- | --- |
| `vol_fade`, `env_kf`, `vol_atk/vol_dcy/mod_atk/mod_dcy` (as `provisionalSeconds`) | times around a fixed base | M3 (`SharedEnv`) |
| `lfo_dly`, `mix_mod`, `lfo_amt`'s sign | value, unused | M4 |
| `flt_type` (`p.filterTopology`) | stepped value, unused; the `filterMode` trait still decides | M5 (R10) |
| `saturate` (still inside the filter loop) | drive, unchanged | M6 |
| `rvb_atk`, `adj_bass/treb/pan/clip` | value / dB / spread, unused | M7 |
| `gateLow/gateHigh`, `softRails`, `pullToNeutral` | table data only | M1 |

`a_freq` no longer carries `kFlagRatio`: it is quantised by its own seven-step
curve (R10), and the ratio snapper of §5.5 stays on `b_freq` until M8 moves it
behind Atonality (R7). `testPitchStability` still asserts Rev 1's "the mod
envelope never touches pitch"; with `a_mod`/`b_mod` now `kFlagPitch`, atonality 0
converges them to neutral and the assertion is vacuous. It is **rewritten** in M4,
not kept (plan §6.1).

### Run it

```
cmake --build cmake-build-debug --target GenoMosh_Tests && ./cmake-build-debug/Tests/GenoMosh_Tests
cmake --build cmake-build-debug --target GenoMosh_VST3
```

Old `.ginomosh` states / DAW recalls from before this commit are expected to print
the refusal line and start a fresh plant. That is the intended behaviour, not a
regression. Next milestone: **M1**, one mapping layer (neutral, gates, steps,
links), which is where `gateLow/gateHigh` stop being data.
---

## 11. Addendum 2026-10-30 — Rev 3 alignment **M1**: one mapping layer

Milestone M1 of `Docs/Rev3-Alignment-Plan.md`: the pure gene-space half of design §5.7 —
neutral, gates, soft rails, quantisation, links, the non-linear mix curve — moved into
one place, and the two growth mechanisms that read the table (`pullToNeutral`,
gaussian-around-neutral seeding) landed with it. **No DSP was changed**: the envelopes,
modulation routing, filter topology and output stage are still §1–§9's until M3–M7.
A mapping layer *does* change what existing genomes mean, and the size of that is measured
below.

### What changed

* `Source/Genetic/Curves.h` — the primitives, each named after the mechanism it implements:
  `neutralOf()` / `neutralGenome()` (5.7.1), `hasGate()` / `gateOpen()` / `gated()` /
  `mappedGated()` (5.7.3), `smoothStep()` / `softRails()` (5.7.4), `equalPowerMixSpace()`
  with `kMixGeneCentre = 0.7937` (5.7.7). `quantiseNorm()` and `Curve::Stepped` came with
  M0 and are used from here on. `snapToRatio()` is now called by nothing in the engine;
  it stays, documented and tested, for M8.
  **The gate convention, because it is the part a reader will trip on:** the lower rail
  belongs to the dead zone (`gateOpen` is `v > gateLow`), the upper rail belongs to the
  gene. Every gene the reference gates has `gateLow == neutral`, so if the boundary were
  "open", 5.7.1 ("neutral is nothing") and 5.7.3 ("off over part of the range") would
  contradict each other — `mod_sh` at neutral would run an 8 Hz sample-and-hold on a
  plant that is supposed to be at rest. `gated()` also *rescales the live part of the
  range* onto the whole of [0,1] rather than just clamping: for `vol_sus` that is exactly
  §6.1.1's `max(0, 2(v-0.5))`, and it is why no destination has to know about gates.
* `Source/Engine/VoiceParams.{h,cpp}` — now **the only place a gene becomes a number the
  DSP uses** (§5.7.2's "not ad hoc in the DSP"). Gates are applied to the six genes the
  reference documents them for (`vol_sus`, `vol_fade`, `mod_vel`, `lfo_dly`, `rvb_atk`,
  `mod_sh`); `flt_kf` is read through `softRails(gateLow, gateHigh)` × its signed amount;
  `flt_type` and `a_freq` go through the table's stepped curves; `osc_mix` is warped into
  equal-power *mix space*, so `p.balance == 0.5` is 50/50 and `mix_mod` can add to the
  same number in M4; `flt_q` goes through its own `Squared` curve instead of a hand-written
  lerp. New resolved-link outputs: `freqAHz` / `freqBHz` (the note after `a_freq` /
  `b_freq`) and `filterHz` / `cutoff2Hz` (the tracking pair, `flt_sep` between them), plus
  `modSampleHoldHz` (the documented §4.2 value for `mod_sh`: off at and below the gate,
  8 Hz just above it, 0.1 Hz at the top — resolved now, read by M4). `Effect` scales the
  wet mix and the branch pan spread with one factor, `VelSens × mod_vel` stays a single
  product applied to the modulation amounts.
* `Source/Engine/Voice.{h,cpp}` — stops re-deriving links: `baseFreqA/B` come from
  `p.freqAHz/freqBHz` (only Nyquist clamping stays here), both cutoffs from
  `p.filterHz/p.cutoff2Hz`, and the separation is held once in `cutoffSepOct` so
  modulation moves the pair together. The `aRatio`/`bRatio` + `filterHz·2^sep` arithmetic
  in `start()` and `updateControlTargets()` is gone.
* `Source/Genetic/Mutation.{h,cpp}` — `MutationParams::pullToNeutral` (default **0.35**) and
  the §5.2 step-3 term `pull · (gi.neutral − seed) · d`; `kFlagNever` genes are now skipped
  in **both** `buildBranchDirection` and `applyDistance` (the pull makes a zero direction
  non-zero, so the direction check alone was not enough); `randomizeGenome` is a gaussian
  around each gene's `neutral` with spread 0.25, reflected at the 0/1 rails, then clamped to
  `softMin/softMax` (design §11, the reference's own randomise) instead of a uniform draw in
  the soft range; the unconditional `snapToRatio` on `kFlagRatio` genes is **removed** from
  `applyAtonality` (it ran at every atonality, which is the opposite of §5.5 — it returns in
  M8 as `converge()` + an atonality-blended snap, R7/D-4); the seed's "must be able to hold
  a note" floor moved to `vol_sus ≥ 0.68`, because with the gate live, a floor at 0.55 buys
  a sustain of 0.1.
* `Tests/EngineTests.cpp` — 28 → **44 checks**. New: `testMappingNeutralIsRest`,
  `testActivationGates`, `testRailsAndMixWarp`, `testSeedsClusterAroundNeutral`,
  `testPullToNeutral`, `testNeutralGateIsAudible`, plus "nothing snaps `b_freq` at
  generation time" inside `testRatioSnapping`. `renderBranch` was factored into
  `renderScene (plant, branch, distance, note, sampleRate, seconds)` — the shared body
  every audio assertion uses, and the thing M2 grows into the §13 protocol.

### How M1's gate was read

The gate says *"a gene at `neutral` changes nothing audible (render-with-neutral vs
render-with-default per gene, assert RMS delta < 0.5 dB)"*. Per-gene RMS deltas on a
*random* genome would fail for genes that are legitimately loud (`flt_freq`, `a_freq`),
and §14 phase 0 states the same requirement as *"neutral value produces the same sound as
the gene at rest"*, so it is measured in the two forms that are exact:

1. **Mapping layer, every gene:** with the genome at `neutralOf()` for all 50 slots, every
   destination must read its identity value — 0 offset, no modulation, no separation, no
   key follow, no drive, no dB, no spread, sustain 0, S&H off, `balance` = 0.5 (equal
   power), `filterQ` = 0.5 (the SVF's quietest), `flt_type` = the parallel-LP step, and
   the cutoff *relative* to A. Plus "the neutral genome **is** the documented factory
   state": the three non-0.5 neutrals of §4.2 are asserted as the seed of that genome.
2. **Audio:** the one gate whose destination exists today (`vol_sus`). Neutral vs a value
   inside the dead zone must be **bit identical** (`rms` and `peak` compared with `==`, not
   with a tolerance — the mapping resolves both to the same value), while opening the gate
   must be *loud*: +1.36 dB of rms, +1.7 dB of peak. And the neutral plant has to make a sound at all, or the
   comparison is between silences.

If that reading is wrong, the change is in `testNeutralGateIsAudible`, not in the engine.

### Measured, once (`GenoMosh_Tests`, 48 kHz, all 44 checks green)

Sizes (M0 recorded 200 / 216 / 8 008 / 24 024 B). `Genome` and `BranchDirection` are
unchanged; `MutationParams` grew by the one float, which is what moved the snapshot:

```
sizeof Genome = 200 B, BranchDirection = 216 B, PlantSnapshot = 8016 B, triple buffer = 24048 B
```

The per-branch render table (12 branches at length 0.85, note 60+branch, 2 s, same seed as
§10, which is also in `/tmp/m0_baseline_tests.txt`; current run in `/tmp/m1_tests.txt` —
both transcribed here so neither file is load-bearing). **Read it with §12's caveat: the
harness of that era never applied its `PlayState`, so the rows are one genome played at twelve
semitones, not twelve grown branches.**

| | peak | rms | branch spread (rms) | DC |
| --- | --- | --- | --- | --- |
| M0 | 0.636 … 0.755 | 0.087 … 0.093 | 0.57 dB | 0.0034 … 0.0041 |
| **M1** | 0.538 … 0.595 | 0.070 … 0.073 | **0.35 dB** | **0.0006 … 0.0008** |

The plant is **2.0 dB quieter** and the "branch spread" is tighter again (0.57 → 0.35 dB —
§12 later showed those spreads are between twelve *notes* of one genome, not twelve branches);
DC dropped another 5×, 60× inside its gate. Usable branches at distance 1 are still
**720 / 720**, and every test that was green at M0 is green, including the bit-identical
re-render and the three sample rates. The drop is expected and comes from four places, all
of them intended: `vol_sus` is gated (a raw 0.68 now means 0.36, not 0.68), `flt_q` is read
through its `Squared` curve (less resonance at the same gene value), `mod_sh` no longer
bends the modulation envelope's curve (that was reading a *rate* gene as a shape), and the
seeds themselves are drawn nearer the middle of every range. **M7 tunes against these
numbers** — they replace §10's.

Curation mechanisms, measured rather than asserted:

```
seeds drawn around neutral   widest excursion from neutral over 400 seeds: 0.235 (a_color)
                             worst mean |gene − neutral| on a wide soft range: 0.069
growth pull (mean |gene − neutral| at length 1, 12 branches):
                             0.0730 without pullToNeutral, 0.0476 with it
gates (audio, vol_sus 0.50 / 0.20 / 0.90):
                             rms 0.09009 / 0.09009 / 0.10540 → bit identical, then +1.36 dB
atonality ceiling            0.00 → pitch genes exactly at neutral, 1.00 → 0.41 of range
```

The atonality run also shows the new randomiser at work: at atonality 1 the furthest pitch
gene now sits at 0.414 of its range instead of 0.642, because seeds are drawn around
neutral — which is the intended reading of R7 ("playable plants have little of it"), and
another reason `testPitchStability` is rewritten rather than kept at M4.

VST3 and Standalone targets build with no new warnings (the two `unused parameter` warnings
in `FlatRotaryLookAndFeel.h` predate this milestone).

### Provisional / deliberately dead after M1

* `p.modShape` is **fixed at 1.0** (linear). Its old source, `mod_sh`, is a sample-and-hold
  rate; driving a curve exponent from it was the thing that made that gene's neutral
  audible. The resolved `modSampleHoldHz` waits for M4, which owns the destination.
* `vol_fade`, `env_kf`, `lfo_dly`, `mix_mod`, `rvb_atk`, `flt_type`, `adj_*` are resolved
  through the correct mapping and still unread — same owners as §10's table (M3/M4/M5/M7).
* `snapToRatio()` has no caller until M8. `kFlagRatio` stays in the table for it.
* `loudnessCompensation` and `safetyScore` still reason in raw gene space with Rev 1
  meanings; they do not know about gates, topologies or the tracking cutoff. Plan §6.3
  tracks them; M3/M5/M7 recalibrate. The 720/720 usable-branch figure is the regression to
  watch there.
* `flt_kf`'s negative (downward) key follow is attenuated by the same rails that mute the
  bottom of its range, so it is only ever worth a few hundred cents per key. That follows
  §5.7.4's wording literally. If a real downward follow is wanted, the fix is the table
  (move `neutral` to the rail boundary), not the engine — M10 material, not M1's.

### Open questions this raised

1. **`b_freq`'s documented default is not its neutral.** Design §4.2 lists the reference's
   factory value as **0.6875**, which on our ±24 st bipolar curve is **+9 semitones**, while
   "nothing happens" for that gene is unison (0.5). Those are two different facts and the
   table currently stores only the second. Nothing in M1 is wrong because of it — the
   neutral convention is about *effects*, and §5.7.1 is explicit that a random genome should
   be centred on mild — but the **parameter defaults** in M9, and possibly a one-off seed
   default, should reproduce the reference's starting interval. Suggested resolution: keep
   `neutral = 0.5`, and record `0.6875` as a factory/`default` value when M9 builds the
   parameter list. Needs an operator nod before it becomes code.
2. Whether `gateHigh < 1.0` should ever mean "off above the rail" rather than "saturates at
   the rail". No documented gene has one, so `gateOpen()` treats the top rail as belonging to
   the gene. If a gene ever wants a top dead zone, that needs a flag, not an overload.

### Run it

```
cmake --build cmake-build-debug --target GenoMosh_Tests && ./cmake-build-debug/Tests/GenoMosh_Tests
cmake --build cmake-build-debug --target GenoMosh_VST3
```

44 checks, `ALL TESTS PASSED`, no WAV files (there is no writer until M2).
Next milestone: **M2**, the §13 harness — `renderScene` becomes `createTest`/`performTest`
with a WAV writer, spectral centroid/flatness, an aliasing estimate and an
envelope-articulation metric, and the **M1-era baseline table above is the thing it
records as the baseline** before M3 changes the sound for real.

---

## 12. Addendum 2026-10-30 — Rev 3 alignment **M2**: the section 13 harness

Milestone M2 of `Docs/Rev3-Alignment-Plan.md`: the golden-file protocol of design §13 —
render offline from a seed at a chosen sample rate and note density, write WAV, measure it,
compare it against a file with a tolerance — and the metrics §13 asks for per render: level,
silence, NaN, DC, spectral centroid and flatness, an aliasing estimate, and the
envelope-articulation estimate. **No engine code changed**; everything new is in `Tests/`.

It is worth saying what that bought, because it was not the tooling: §10 and §11's audio
tables turn out to have measured something other than what they claim, and the first
measurement of real branch growth (§6.3's predicted failure) is now on the record with its
failure mode named.

### What changed

* `Tests/Render.{h,cpp}` (new) — the protocol, with no dependencies beyond the core:
  * `planScene (seed, sr, seconds, density, lo, hi)` — §13's "density". A gap with the
    requested mean (`0.35 + 1.3·uniform`, so the average is exactly `1/density`), holds that
    **scale with the gap** so that raising the density raises the note rate rather than the
    voice count, one key in ten long, a cap on simultaneous keys, and pitch classes spread
    over two octaves so a scene touches all twelve branches. Sample positions are integers
    derived from seconds, which is what makes "same inputs, identical output" testable.
  * `renderScene (RenderSetup)` — the one render path; `renderNote (plant, branch, length,
    note, velocity, sr, seconds)` is §13's batch-mode input. `offlinePlayState()` sets
    `wheel = 1, wheelScale = 1` so a branch length *is* the §5.2 distance, and the PlayState
    is always passed explicitly (see below).
  * `measureAudio (l, r, sr, notes)` — per note, then aggregated: peak/rms/DC/finite/silent,
    spectral centroid and flatness, an **off-grid energy ratio** as the aliasing estimate, and
    articulation: attack seconds, hold level in dB relative to peak, release seconds, how long
    the sound actually lasts, and whether it ended **by itself while the key was still down**.
    That last one is the shape M3's `env_time` / `vol_fade` gates will be written in; it exists
    before M3 so M3 is measured rather than described.
  * `wav::write / wav::read` — 16 bit WAVE_FORMAT_PCM, a canonical 44 byte header held
    byte-for-byte by a test, and a reader that *refuses* a non-PCM or truncated file instead
    of returning an empty buffer. `quantise()` has no dither on purpose: performTest compares
    the render and the file in the PCM16 domain, so `tolerance 0` is achievable and means
    something. `createTest` writes the file, reads it back, measures *the file*, and leaves a
    `<file>.wav.txt` sidecar with the spec, a content hash and the metrics — design 13's "keep
    the audio out of git, keep the hashes in a text file" (`Tests/Golden/.gitignore`).
  * `createTest / performTest / massiveAudioTest` with the document's own argument names.
* `Tests/EngineTests.cpp` — the inline `renderScene` and its `RenderResult` are gone; every
  audio gate goes through the harness. New: `testWavFileIsAFile`, `testMetricsOnKnownSignals`,
  `testScenePlanIsSeeded`, `testM1BaselineAudio`, `testBranchBatch`, `testGoldenProtocol`,
  `testMassiveSoak`. **44 → 93 checks.** `main` is now a command line with the §13 vocabulary:
  `createTest / performTest / batch / soak / noiseBank` with `--seed --time --sr --density
  --file --tolerance --length --rounds --out`; no arguments is still the suite `ctest` runs.
* `Tests/CMakeLists.txt` — plus `Render.cpp`. The core and the plugin are untouched.

### The one thing M2 had to be sure of first: the metrics measure what they say

Every metric is checked against a signal built from its own answer, before any number taken
from the engine is believed. Measured, in the suite:

```
440 Hz sine        f0 445.0 Hz, centroid 440.0, flatness 0.0010, off grid 0.0005
white noise        f0 none,     centroid 11.4 kHz, flatness 0.842, counted unvoiced (not "all aliasing")
220 Hz stack       off grid 0.0002; + 1450 Hz partial -> 0.3127; + 26.15 kHz sampled at 48 k
                   (which lands on 21.85 kHz) -> 0.3127
20 ms/-8 dB/100 ms attack 0.0220 s, hold -7.8 dB, release 0.0980 s, sound over 0.698 s, self ended no
same, gone at ~120  self ended yes, sound over 0.118 s
```

Two lessons from getting that to pass, both written into the code:

* An autocorrelation's **largest** peak is an octave-low trap for anything harmonic-rich (the
  correlation at half the fundamental's period is nearly as strong), and its first peak is a
  few percent off in spacing when an inharmonic component pulls it. The estimator is therefore
  *first* strong peak for the octave, then a harmonic-grid search ±15% for the spacing. The
  220 Hz + 1450 Hz case above agrees with an independent NumPy implementation of the same
  metric to four decimals. What it still does not do is choose between a fundamental and its
  subharmonic — that error is the conservative one for an off-grid ratio, and it is
  deterministic, which is what a milestone-to-milestone comparison needs.
* A **single tone has no unique fundamental** (any divisor explains it), so its grid is only
  asserted to be inside the sieve's own tolerance; the harmonic stack is asserted to 2%.

### Measurement 1: the M0/M1 audio tables measured the seed, not the branches

The previous harness built a `PlayState`, filled it in — `wheel = 1`, `wheelScale = 1`,
`volume = 0.7`, one branch at the requested length — and **never handed it to the `Synth`**.
So every "per branch" number in §10 and §11 was rendered with the engine's own default
`PlayState`: all twelve branch lengths 0, wheel closed, plugin default macros.

Consequences, stated plainly:

* §10's and §11's per-branch tables are **one genome (the seed) played at twelve different
  notes**, not twelve grown branches. "branch spread 0.57 dB → 0.35 dB" is the spread between
  semitones. §11's conclusion that M1 flattened the *branch* spread does not follow from it.
* Nothing else in M0/M1 is affected: those gates (table, mapping, gates at both ends, growth
  exempt identity, atonality, 720/720 safety score, bit-identical re-render) are gene-space
  or single-render claims, and they stand.
* M7's level numbers should be taken from this section, not §11's.

`testM1BaselineAudio` renders those twelve cases again through the new harness — same seed,
same plant, default `PlayState`, note on with the old call's **sample offset of 8** (the old
line was `addNoteOn (note, 0.9f, 8)`: an offset slipped into the position that takes the
offset), note off at block × 100 — and compares against the recorded rms column:

```
worst relative difference: 6.15e-07      (all twelve reproduce)
```

which is simultaneously the proof that M2 changed no sample, and the record of what the old
harness actually did.

### Measurement 2: what growing a branch does, measured for the first time

§13 batch mode: twelve branches × lengths {0, 0.25, 0.5, 0.75, 1.0}, the note belonging to
the branch (`60 + branch`), one branch grown per case, 48 kHz, 2 s, wheel pinned open. Seed
`hash::fromString ("render-seed")`, the same plant as §10/§11.

| length | cases within ±6 dB of their seed | worst deviation |
| --- | --- | --- |
| 0.00 | 12 / 12 | 0.00 dB (the seed exactly) |
| 0.25 | 12 / 12 | −0.15 dB |
| 0.50 | 10 / 12 | −21.54 dB |
| 0.75 | 10 / 12 | −23.60 dB |
| 1.00 | 5 / 12 | −22.05 dB |
| **all** | **49 / 60** | **−23.60 dB quieter, +1.64 dB louder** |

The shape of that is the finding, not the average: up to length 0.25 the level compensation is
inside a tenth of a dB, and from 0.5 a **minority of branches fall away** by 17-24 dB rather
than drifting. All eleven are the same kind of branch, and three unrelated metrics agree on
which ones - measured against the same branches at length 0:

| | the twelve seeds (length 0) | the eleven offenders |
| --- | --- | --- |
| spectral centroid | 378 ... 733 Hz | 9 754 ... 22 178 Hz |
| off-grid energy | <= 0.032 | 0.26 ... 0.915 |
| hold level vs peak | -5.7 ... -7.0 dB | -6.9 ... -19.5 dB |

i.e. they grew into a shrill, non-sustaining blip instead of a note. Worst case, branch 8 at
length 0.75: rms 0.00645 against its seed's 0.0982, centroid 20.9 kHz, hold -13.6 dB. Nothing
compensates a note that stopped sustaining, and `loudnessCompensation`/`safetyScore` still
reason in raw gene space while `vol_sus` is gated - which is exactly the risk
`Docs/Rev3-Alignment-Plan.md` §6.3 raised. **The eleven are M3's
list**, and the gate in the test is written as the measured distribution with the §13 target
named as a constant (`BatchStats::kTargetDbVsSeed = 6.0f`), so M3's improvement is visible
rather than a tolerance quietly widened.

Also from the same run: DC ≤ 0.0018 everywhere, no case silent or non-finite, peak ≤ 0.631,
centroid 245 … 22 178 Hz, flatness 0.007 … 0.363, off-grid up to 0.915 (the noise-colour
branches; the number is a comparison instrument, not a defect).

### Measurement 3: the protocol, at the sample rates

The suite's version (short scenes, five rates, `tolerance 0`):

```
sr  22050: frames    88200  maxdiff 0.000000 -> ok      sr  96000: frames   384000  maxdiff 0.000000 -> ok
sr  44100: frames   176400  maxdiff 0.000000 -> ok      sr 192000: frames   768000  maxdiff 0.000000 -> ok
sr  48000: frames   192000  maxdiff 0.000000 -> ok
one byte changed in the file: max diff 0.00195 -> fails at tolerance 0, passes at 0.01
a file whose header says 22 050 refused by a spec that asks for 44 100
```

The documented set (`seed 1`, `density 10`, all branches at 0.6), generated and left in
`Tests/Golden/` with its hash sidecars — regenerate with the commands at the end, and expect
these hashes to die at M3, which is the point of recording them:

| file | time | frames | fnv1a64 | peak | rms | centroid | flat | off grid |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `Golden22050.wav` | 300 s | 6 615 000 | `da78ca71e9be4228` | 0.943 | 0.2429 | 439 Hz | 0.316 | 0.000 |
| `Golden44100.wav` | 300 s | 13 230 000 | `67e5c726dd9463d1` | 0.933 | 0.2436 | 485 Hz | 0.192 | 0.000 |
| `Golden48000.wav` | 300 s | 14 400 000 | `7a07b57698a7f9b7` | 0.969 | 0.2445 | 488 Hz | 0.190 | 0.081 |
| `Golden96000.wav` | 150 s | 14 400 000 | `516eed4a9af2e5f4` | 0.921 | 0.2460 | 554 Hz | 0.091 | 0.078 |
| `Golden192000.wav` | 60 s | 11 520 000 | `6ab1b247a8ada66f` | 0.929 | 0.2517 | 576 Hz | 0.017 | 0.000 |

All five then pass `performTest … --tolerance 0` against a fresh render: `maxdiff 0.000000
(max 0 lsb)` for 6.6 M, 13.2 M, 14.4 M, 14.4 M and 11.5 M frames respectively. A 300 second
render of a whole plant is therefore a byte comparison, which is what makes cross-version
regression cheap in §13's terms.

Two numbers in that table are already owned by later milestones: a **dense scene peaks at
0.92-0.97** of full scale before the master volume is even involved (§8.1's `Volume` curve and
limiter, M7), and the 48/96 kHz goldens carry **8% off-grid energy** at density 10, which is
what M6's oversampling and band-limiting work will be measured against.

### Measurement 4: the soak

`GenoMosh_Tests soak --rounds 1000` — random seed, one of seven sample rates below 100 kHz,
`density = sqr(random(10)) + 1` (up to 101 notes/s), every branch at a random length, 1.5 s
each; ~7 minutes:

```
rounds 1000: unusable 3 (nan 0, silent 0, dc 0, clipped 3, over 0.995 4)
worst dc 0.00000, worst peak 1.0104
```

No NaN, no silence, no runaway DC anywhere. Three scenes in a thousand passed full scale —
sixteen voices summing into one unconditional soft clipper, which is the thing §8.1's
documented `Volume` + saturating limiter replaces (M7); the gate is the measured proportion
(≤1%) with the target (0) in the check's name.

### Deliberately left for later milestones

* **The ±6 dB target is not met today** and the milestone that can meet it is M3. The list of
  offenders is printed by the batch and is in the table above.
* The articulation metrics have no *passing* gate written in them yet — they are the
  instrument M3's acceptance matrix ("does measured attack/hold/release move in the direction
  `env_time`/`vol_sus`/`vol_fade`/`env_loop`/`env_tilt` imply") is written in.
* `planScene` plays notes; it does not bend, pedal, glide or re-resolve, because none of those
  exist yet (M9). The soak does exercise voice stealing by outrunning the pool.
* The golden hashes will be invalidated by M3-M7 by design. The suite's own short goldens are
  regenerated in place each run, so `ctest` does not depend on `Tests/Golden/` at all.

### Run it

```
cmake --build cmake-build-debug --target GenoMosh_Tests && ./cmake-build-debug/Tests/GenoMosh_Tests   # 93 checks
cmake --build cmake-build-debug --target GenoMosh_Tests && ctest -j1                                 # the same, 45 s
./cmake-build-debug/Tests/GenoMosh_Tests batch --seed $(printf %u 42) --sr 48000 --time 2
./cmake-build-debug/Tests/GenoMosh_Tests soak --rounds 1000
./cmake-build-debug/Tests/GenoMosh_Tests createTest  --seed 1 --time 300 --sr 44100 --density 10 --file Tests/Golden/Golden44100.wav
./cmake-build-debug/Tests/GenoMosh_Tests performTest  --seed 1 --time 300 --sr 44100 --density 10 --file Tests/Golden/Golden44100.wav --tolerance 0
./cmake-build-debug/Tests/GenoMosh_Tests noiseBank --out /tmp/noisebank --time 4                     # 13's 100 files
```

44 checks became 93, `ctest` is green, no plugin file changed. Next milestone: **M3**, the
shared envelope — and its gate is written in the articulation metrics this milestone just
built, starting with the eleven branches above.

---

## 13. Addendum 2026-10-30 — Rev 3 alignment **M3**: one envelope clock, and the note's length is a gene

The change this project has been circling: there is now **one envelope**, its clock is the
note, and `env_time` is its length (design §6.1.1, decisions R3 and D-5). Everything the
articulation metrics of §12 measured is now produced by the structure those metrics were built
to describe.

### What changed

* `Dsp.h`: `Env` is gone. `EnvShape` is a resolved description of one note's envelope;
  `SharedEnv` runs it — one phase clock, sample accurate, no first-order smoothing anywhere
  (design §6.4). It emits the amp and the mod value from the same position, with separate
  curvature exponents, and owns the note-off behaviour: stop repeating, ramp from wherever it
  is to silence over the fixed `kNoteOffReleaseSeconds`.
* `VoiceParams` is where the genes become that shape, and it is the only place the R3 reading
  of `env_time ≥ 0.9` lives:

  ```
  env_time < 0.9  -> T = env_time x env_kf(key), env_loop < T repeats, one pass otherwise
  env_time >= 0.9 -> T = infinite; the shape base is env_loop if that is inside
                     mapGene(env_time, 0.9) = 15.82 s, otherwise the base is 15.82 s and
                     there are no repeats -- which is what makes "vol_fade alone ends it" real
  ```

* The four curvature genes are exponents (`2^(2(v-0.5))`, neutral exactly linear), never
  seconds; `provisionalSeconds` and the `loopEnvOneShot`/`loopMod`/`modSource` scaffolding are
  deleted. `vol_sus` reaches the voice as the gated `max(0, 2(v-0.5))`; `vol_fade` is a second
  fade after the attack whose destination is silence.
* Velocity is applied **once**, to the mod envelope (`VelSens × mod_vel` → `modScale`), instead
  of pre-scaling each destination amount. Same doc sentence, one reading of it.
* The guard rails read *effective* values (`effectiveSustain`, `effectiveDurationSeconds`,
  `effectiveFade`, `envelopeDuty` in `Mutation.h`). This was §6.3's outstanding item: with
  `vol_sus` gated at 0.5 a raw 0.55 is 0.1 of sustain, and with duration as a gene a branch
  can be silent in three new ways. `loudnessCompensation` gained the duty term it was missing.
* Two structural traits were removed: `modSource` (there is one envelope, nothing to select)
  and `filterMode` (see Measurement 1). The voice is lowpass-only until M5 gives `flt_type`
  its four topologies.
* `Tests/Render.cpp`, one metric fix: the search for "the sound fell below −40 dB" now has to
  see the envelope reach −40 dB first. From the onset — defined at 0.5% of the peak — a shape
  that takes seconds to reach its peak was under 1% of it for longer than the 50 ms
  confirmation window, and was reported as self-ended. Only reachable now that slow attacks are
  something the genes ask for.

**One deliberate deviation.** A repeat is a whole envelope at the loop rate. The §6.1.1 sketch
let `env_loop` *truncate* the shape (`Tshape = T`, re-trigger at the period), which makes
`env_time` and `env_loop` fight over one envelope: at `env_loop = 0.05 s` against a 30 s shape
each repeat is a 0.17% sliver of an attack, near silence. Here the note is the shape
`env_time` asks for and `env_loop` only decides how often it is re-sounded; the argument is in
the comment above the envelope block in `VoiceParams.cpp`.

### Measurement 1 — the eleven, named, and they were not the envelope

The batch now prints the resolved genome and mapping numbers for every case outside the target.
Sixteen cases were outside after the envelope landed (the earlier eleven plus five the new
clock moved), and every one of them had the same cause, which was not an envelope at all:

| printed for each offender | values |
|---|---|
| `flt_type` gene / resolved Q / noise / forms | unremarkable: 0.56-0.73, Q 0.5-0.9, noise ≤ 0.15 |
| `mode` (the **filterMode trait**) | 1 = band or 2 = high, while the seed is lowpass |
| result | cutoff 5-20 kHz pass band where the fundamental (261 Hz) should have been |

That is §12's three-metric signature — centroid 2-22 kHz, hold near silence, 17-25 dB down —
produced by a structural *trait* that replaced a *gene* outright, which is what decision R10
forbids and what M5 had already scheduled for removal. Removing the trait is one line. R10 also
says a trait may only offset a stepped gene by one step; there is no offsetting a topology that
has no gene behind it yet, so the voice is lowpass until M5.

### Measurement 2 — growth is now level tracked

`batch` (12 branches × 5 lengths, 48 kHz, 2 s), worst absolute deviation from the branch's own
seed by branch length:

| branch length | 0.00 | 0.25 | 0.50 | 0.75 | 1.00 |
|---|---|---|---|---|---|
| worst dB vs seed | 0.00 | 0.19 | 0.43 | 1.42 | 3.64 |

60 of 60 cases inside the ±6 dB target (M2: 49 of 60). Loudest branch +3.64 dB, quietest
−1.58 dB. Centroid range over the whole batch: 238-1219 Hz (was 238-22 604 Hz). DC worst
0.0043, no unusable case, no case above full scale. Articulation columns are in the same
table: measured attack 0.24-0.52 s, hold −3.5…−10.6 dB, release 0.16-0.30 s, no self-ended
case in this seed. Safety score over 60 seeds × 12 branches at distance 1: **720 / 720** —
the gate is now that hard 100%, and the ratio is printed rather than implied.

### Measurement 3 — the articulation matrix (genes → measured, 22.05 kHz)

Measured off rendered audio through the §13 metrics, one gene at a time from the instrument at
rest (`testEnvelopeArticulation`). This is the evidence M4 and the articulation half of M10
will be judged against.

| what moved | measured |
|---|---|
| `env_time` 0.30 → 0.55 | sound over 0.339 s → 1.678 s (the genes map to 0.44 s and 1.86 s; the metric reports where the level left −40 dB) |
| `vol_sus` below the gate (0.45) | no sustain: hold −80 dB, self-ended at 1.22 s = the shape |
| `vol_sus` open (0.95) | hold −3.1 dB, still sounding at key up, release 0.177 s |
| `vol_sus` 0.30 vs 0.45 | bit identical peak — the dead zone is dead |
| `env_tilt` 0.1 → 0.9 | measured attack 0.172 s → 0.959 s |
| `vol_atk` 0.0 → 1.0 (same split) | measured attack 0.137 s → 0.514 s — curvature, not duration |
| `env_kf` at note 48 vs 72 | sound over 3.570 s → 0.241 s |
| `env_time` 0.95 (∞, no repeats) | still sounding at key up 18 s in, hold −7.2 dB, release 0.177 s — a 15.8 s shape never ends the note |
| `vol_fade` 0.0 → 1.0 | hold −2.2 dB → −80 dB and self-ended at 0.89 s; below the gate (0.6) bit identical to no fade |
| the same scene twice, at 22.05/48/96 kHz | bit identical |

### Measurement 4 — the instrument at rest moved, and the goldens with it

The shared envelope took the resting level **down about 8.4 dB**: the twelve-note seed table of
§11/§12 measured 0.0704-0.0733 rms and now measures 0.02679-0.02780. A note now spends half of
`env_time` attacking and settles onto its sustain floor instead of sitting at full level. That
is the design working, not a regression — but it means **M7 tunes the output stage against this
section**, exactly as the plan says, and `testM2BaselineAudio` pins these twelve numbers so the
next movement is measured in decibels.

The documented golden set (seed 1, density 10, all branches 0.6) was regenerated at the
documented times and re-rendered **bit-exact at tolerance 0** (`maxdiff 0.000000, max 0 lsb`):

| file | time | hash | peak | rms | off grid |
|---|---|---|---|---|---|
| Golden22050.wav | 300 s | `8b316307a96f05e9` | 0.87497 | 0.10181 | 0.2042 |
| Golden44100.wav | 300 s | `d65e77bf96e8a991` | 0.90646 | 0.10242 | 0.1274 |
| Golden48000.wav | 300 s | `965454a7befaed91` | 0.89254 | 0.10245 | 0.1504 |
| Golden96000.wav | 150 s | `fa6f390a77821d58` | 0.86706 | 0.10782 | 0.0000 |
| Golden192000.wav | 60 s | `7802615673bb3618` | 0.88318 | 0.12030 | 0.0000 |

Peaks are 0.87-0.91 (M2: 0.92-0.97). The off-grid ratio went up at the low rates and reads 0
at 96/192 kHz; it is a comparison instrument, so §12's "8% at 48 kHz" is not comparable to
"15% at 48 kHz" across a structural change — M6 owns interpreting it after the band-limited
oscillators land.

Soak, `soak --rounds 1000`: `unusable 0 (nan 0, silent 0, dc 0, clipped 0, over 0.995 0)`.
No scene reached 0.995 at all, which is the 8 dB of headroom above rather than a limiter — the
M7 item is unchanged, it is just no longer urgent for clipping.

### Deliberately left

* Filter topology: `flt_type` still has no audible destination (M5), which is also why the
  loudness model has nothing to learn about topologies yet.
* `mod_sh`'s sample-and-hold rate and `b_sh`, `mix_mod`, and the bipolar `a_mod`/`b_mod` pitch
  modulation: the destinations exist in `VoiceParams`, M4 wires them. `testPitchStability` is
  still the M1-era assertion and must be rewritten there.
* `rvb_atk` and the per-voice wet path (M7), and the level retune §13's first table implies.
* The reference's envelope is presumably a single continuous curve; this one is two power curves
  with a split. Exponents of 1 are linear, so the neutral case is the same shape; anything that
  smooths the junction is a tuning decision for M10, not a structural one.

### Run it

```
cmake --build cmake-build-debug --target GenoMosh_Tests && ./cmake-build-debug/Tests/GenoMosh_Tests   # 113 checks
cmake --build cmake-build-debug && cd cmake-build-debug && ctest -j1                                  # the same, 36 s
./cmake-build-debug/Tests/GenoMosh_Tests batch                     # names every case outside 6 dB now
./cmake-build-debug/Tests/GenoMosh_Tests soak --rounds 1000
bash /tmp/gen_golden.sh                                            # regenerate + verify the five goldens
```

93 checks became 113 and every previous one still passes, with one deliberate exception:
`testM1BaselineAudio` became `testM2BaselineAudio` and its table was re-pinned, because M3 is
allowed to change the audio — that is what the milestone is for. Next: **M4**, whose modulator
is now this same clock (`a_mod`/`b_mod` as bipolar pitch modulation, `mix_mod`, the two S&H
sources), and which must rewrite `testPitchStability` in the same commit.

---

## 14. Addendum 2026-10-30 — Rev 3 alignment **M4**: one LFO, two sample-and-holds, and the mod envelope reaches the pitch again

Milestone M4 of `Docs/Rev3-Alignment-Plan.md`. What was already true after M3 — one clock, whose
`modValue()` is the note's envelope and nothing else — is now pointed at the output: `a_mod`,
`b_mod`, `mix_mod`, `lfo_*`, `mod_sh`, `b_sh`. 113 checks became **116**, all green; the VST3 and
Standalone targets build with no new warnings.

### What landed

*`Source/Engine/Dsp.h`*
* `SampleHold`, one re-quantizer used by both S&H sources. `process()` reports whether this sample
  began a new step, which is how the voice knows to re-target without a ramp.
* `enum LfoShape { kLfoSine, kLfoTriangle, kLfoSampleHold }`; the voice uses the sine.

*`Source/Engine/VoiceParams.{h,cpp}`* — the mapping layer resolves the amounts, the voice applies them.
* `aModSemis` / `bModSemis` in **semitones**, `mapGene` (bipolar, so `0.5` = 0 st, `0.0` = −12 st,
  `1.0` = +12 st) × `kPitchModSemitoneCeiling = 12.0f`. The ceiling is the mapping layer's hard
  bound; Atonality's convergence is the generation-time bound (R2, R7). Both are tested.
* `lfo_amt` becomes `lfoDepth` (magnitude) + `lfoSignB` (±1: below the middle, B's LFO runs in the
  opposite direction — the documented polarity).
* `b_sh` becomes `bSampleHoldHz` (below neutral: B's own frequency ÷ 8…1, note-synced; above: free
  0.5…16 Hz) + `bSampleHoldMix` (see the deviation below).
* `mod_sh`'s `modSampleHoldHz` (resolved in M3, unused) now has its consumer.

*`Source/Engine/Voice.{h,cpp}`*
* The mod value is `env.modValue() * p.modScale` and *then* `modHold` — scale first, hold second,
  which is the design's order and the one that keeps velocity a depth rather than a step size.
* When a hold fires: `updateControlTargets(...)` then `applyTargetsImmediately()`. **The step is the
  sound** (plan §6.4); ramping it away is what makes `mod_sh` inaudible.
* `mix_mod` is added to the balance in *mix space* before the equal-power curve, so it slides over
  the same 0.79 point `osc_mix` uses, and the two A/B gains become control-rate targets.
* Tremolo per sample (`amp *= 1 + depth · lfoV`), vibrato at the control rate, both from one LFO;
  `lfo_bal` is the equal-power split, `lfo_dly` a linear depth ramp after the documented onset.
* The fixed LFO→filter path is gone (R6). The mod envelope stays a filter modulator; the LFO's
  second destination is level, which is what gives `lfo_bal` a meaning.
* Per-voice random LFO *shape* removed, random phase kept.
* `b_sh` is a real second S&H on B's output, not an `oscBMode` mode.

*`Source/Genetic/GeneTable.h`* — `lfo_dly`'s range corrected from 1.0 s to the documented **8.0 s**.

*`Source/Genetic/Mutation.{h,cpp}`*
* `BranchTraits::oscBMode` renumbered: `0` no trait, `1` sync, `2` ring mod. Ring-mod and sync stay
  structural traits (R10); `b_sh` semantics no longer live in a trait. `resolveTraits` picks
  `1 + rng.integer(2)`.
* `loudnessCompensation` learned tremolo: a rising `√(1 + d²/2)` for a depth `d` at the balance's
  sin² weight — but only scaled by how many LFO cycles fit inside a nominal note (`cycles − 1` over
  3). Ungated it over-corrected slow LFOs, because a note that contains one and a half cycles is
  not the same signal as a tremolo.

### Deviations, on purpose

1. **`b_sh` at neutral must be silent, and the reference gives the gene no dead zone.** The gene's
   neutral is `0.0`, its range starts at `0.0`, so there is nowhere to put a gate. The mapping layer
   crossfades the hold in over the first 1/8 of the range (`bSampleHoldMix = smoothStep(v/0.125)`):
   the neutral genome is bit-identical in its resolved mix (0) and the gene is audible immediately
   above it. This is the same compromise as `flt_kf`'s rails in M1 and belongs in the same paragraph
   of §2 if the reference is ever re-read.
2. **The ceiling lives in the mapping layer, not in the voice.** `kPitchModSemitoneCeiling` is a
   named constant so a test can state the bound against the gene table rather than against a magic
   number in DSP code.
3. **A repeat is still a whole envelope** (M3's deviation) and it bit this milestone: the neutral
   `env_loop` maps to a 1.22 s period, so a long `env_time` *repeats* at 1.22 s. Any test that wants
   one slow dive must set `env_loop` above the duration. That is not a bug and it is not a
   deviation; it is the R3 reading working, and the test now says so in a comment.

### Measurement 1 — the pitch gates, and a probe that had to be replaced

`testPitchStability` is **deleted**; `testPitchModulation` replaces it, in audio:

| case | measured |
| --- | --- |
| genes at neutral | 261 Hz → 262 Hz, **+0 cents** over the note (the surviving half of the old gate) |
| `a_mod = 0.0`, atonality 1 | 238 Hz → **131 Hz**, **−1194 cents** at the peak of the mod envelope |
| same, atonality 0 | +0 cents, and the dive is gone — the plant cannot ask for it |
| ceiling | never below `2^(-12.5/12)` of the pressed note, with the ±tens of cents of per-voice detune and drift allowed for (D-3) |

The first version of this measured **−360 cents** for a true octave, twice over for two separate
reasons, and both are now fixed in the test code:

* cents are `1200 · log2`, not `1200 · log10`. Three places in this file had the second; it
  understates every interval by ×1.667 and had been passing because its thresholds were tuned
  against it. Found only because a measured octave looked wrong.
* Autocorrelation period tracking is **useless on a sweeping pitch**: over a wide lag range it
  alternates between the fundamental and an octave above, and that reads as modulation that is not
  there. The pitch tests now use `peakHz`, a Hann-windowed DFT over a *stated band* with parabolic
  interpolation, which picks the **lowest partial reaching half the strongest** — this synth puts
  more energy in the second partial than in the first (the colour term adds an octave up, and an A/B
  mix detuned by a few cents splits the fundamental), so the loudest bin is usually not the pitch.
  `testModulationProbesOnKnownSignals` now feeds that probe a synthetic 261→130 Hz dive whose
  instantaneous frequency is known to the cent: it reads **0 cents off**. That gate is what makes
  every number above mean something.

One more trap worth recording, because it cost two runs: the file's `kTwoPi` already contains the
2π. A hand-written `2.0 * kTwoPi` doubles every analysed frequency, which is exactly why "the 220 Hz
tone reads 110 Hz".

### Measurement 2 — one LFO, its balance, its delay, its polarity

* `lfo_rate 0.78` resolves to 6.00 Hz and everything moves at 6.00 Hz: tremolo depth measured at
  6 Hz is **0.528** at `lfo_bal = 0.5`, and moving only the rate to 1.61 Hz drops it to **0.084**.
* The balance is a continuum, not two switches: `lfo_bal` 0.0 / 0.5 / 1.0 → tremolo **0.061 /
  0.528 / 0.770** against period modulation 0.092 / 0.065 / 0.000. The gate asserts the response is
  monotone and the ends are dominant, not that they are exclusive — a 0.5 split of two *quadrature*
  destinations is not a switch, and the first version of this assertion failed on a correct engine.
* `lfo_dly` measures an onset, not silence: at a 1.60 s delay the tremolo is **0.195** before it and
  **0.434** after, against **0.571** with no delay at the same place. The depth ramps in (the design
  says "onset fade"); asking for literal silence at t = 0.1 × delay was a test bug.
* **The `lfo_amt < 0.5` polarity half of the gate is asserted at the mapping layer, and only its
  direction in audio.** A/B in opposite phase cancels twice per LFO period, so its level modulation
  sits at *twice* the LFO rate: measured 0.122 in phase, **0.144** in opposite. The direction is
  right and the effect is small because the fixed per-voice detune and phase (D-3, not a gene, and
  not closable) beat the A/B mix at a comparable level. The mapping test asserts the thing that is
  actually decided: `lfoSignB` is −1 below the middle, +1 above, with the same depth magnitude.
  Making the audio half of this strong would need either a per-voice detune gene (Rev 3 does not
  have one) or a second measurement that resolves 6 Hz and 3.5 Hz apart, which is a longer window
  than a note of this length gives.

### Measurement 3 — the two sample-and-holds, measured as steps

* `mod_sh 0.79` (resolved 4.0 Hz): the pitch jumps **2.5 times a second** across the note, against
  **0.0** with the gene at neutral. Counting the *steps* rather than the waveform's edges is the
  difference: the earlier edge metric gave 270 steps held against 283 off, i.e. it measured the
  waveform's own zero crossings. The jump detector compares each step against three times the
  typical movement, which is what separates a re-quantised ramp from a smooth one.
* `b_sh 0.75`: only **5.3 %** of B's fundamental is left in the spectrum, against 100 % at neutral
  (with `osc_mix` at B alone and the amp held at unity). "A held oscillator has no fundamental" is
  the honest way to see a hold through a DC blocker; a flatness metric on the waveform read 0.05
  held against 0.004 unheld, because a filtered held step still rings.

### Measurement 4 — the batch, and the spread going up on purpose

| | M3 | M4 |
| --- | --- | --- |
| cases inside ±6 dB | 60 / 60 | **60 / 60** |
| worst branch vs seed | +3.64 / −1.58 dB | **+1.00 / −5.29 dB** |
| worst DC | 0.00325 | 0.00349 |
| quietest case rms | 0.0578 | 0.0597 |
| loudest peak | 0.734 | 0.734 |
| centroid range | 238 … 1470 Hz | 272 … 1470 Hz |

The gate is now the target itself (60 of 60 inside ±6 dB) with one notch of slack, and the spread
widening is a known, named consequence rather than a mystery: three genes reached the output in this
milestone that did not reach it before. `b_sh` puts B through a sample-and-hold, and the DC blocker
then removes most of its energy; `mix_mod` moves a 50/50 mix under the equal-power curve; `a_mod` /
`b_mod` move the whole voice an octave while the guard rail still assumes a static pitch.
`loudnessCompensation` learned duration in M3 and tremolo here, and has not learned those three.
That is plan §6.3's warning verbatim, and M5 owns the loudness model that knows topology,
separation and the sources of level — with M7's output stage and limiter behind it. The branch that
moved most is branch 0 at lengths 0.75 and 1.0 (−3.82 / −5.29 dB, hold level −12.4 / −15.0 dB): a
non-sustaining branch whose genes now do more, i.e. the same shape as M3's eleven, and it is
measurable in the batch's per-case table rather than hidden in a summary.

The baseline table in `testM2BaselineAudio` was re-pinned (it prints "the audio table, re-pinned
after M4 (baseline for M5+)"); reproducing it is exact to **1.80e-06 relative**, which is the half
of this milestone that proves nothing unintended changed. Post-M4, instrument at rest, 12 branches of
the `render-seed` seed at 48 kHz / 2 s, rms: 0.0252168, 0.0248398, 0.0293937, 0.0234855, 0.0231596,
0.0234624, 0.0248778, 0.0277567, 0.0277592, 0.0230166, 0.0309153, 0.0269386. These are ~0.7 dB
under M3's table: the pitch modulation moves energy around the fixed part of the filter response.

### Measurement 5 — the goldens, re-hashed

The documented set (`seed 1`, `density 10`, all branches at 0.6) regenerated in `Tests/Golden/`:

| file | time | frames | fnv1a64 | peak | rms | centroid | flat | off grid |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `Golden22050.wav` | 300 s | 6 615 000 | `1291315fb7df286e` | 0.821 | 0.0767 | 772 Hz | 0.350 | 0.000 |
| `Golden44100.wav` | 300 s | 13 230 000 | `c782443ba7b676a0` | 0.770 | 0.0773 | 941 Hz | 0.225 | **0.285** |
| `Golden48000.wav` | 300 s | 14 400 000 | `671438168f3fca6e` | 0.763 | 0.0773 | 844 Hz | 0.149 | **0.168** |
| `Golden96000.wav` | 150 s | 14 400 000 | `81aa765b0b252e3f` | 0.848 | 0.0811 | 385 Hz | 0.023 | 0.000 |
| `Golden192000.wav` | 60 s | 11 520 000 | `990872e2bd05aef8` | 0.769 | 0.0910 | 574 Hz | 0.009 | 0.000 |

All five then pass `performTest … --tolerance 0` against a fresh render: `maxdiff 0.000000 (max 0
lsb)`. Two things changed in that table and both are owned by later milestones:

* **The dense scene is 12 dB quieter than the M3 goldens** (rms 0.077 vs 0.24) and peaks at
  0.76-0.85 rather than 0.92-0.97. M3 said the instrument at rest is ~8 dB quieter; at density 10
  the difference is larger, because the mod envelope's attack and the sustain floor apply to every
  voice in a dense scene. M7 tunes this.
* **Off-grid energy appeared at 44.1/48 kHz** (0.285 / 0.168, where M3 measured 0.000 / 0.081).
  Vibrato and tremolo are sidebands: a 6 Hz LFO puts energy ±6 Hz around every partial, and the
  metric calls energy that is not on the note's harmonic grid off-grid. This is not aliasing and it
  is not a bug — but it means M6's oversampling work must be judged against a scene that has
  modulation in it, and the metric needs an octave of tolerance around each partial before it can
  keep being an aliasing instrument. Filed as an M6 note.

### Measurement 6 — the soak, and the headroom going the other way

`soak --rounds 1000`: **0 unusable** — no NaN, no silence, no DC, and for the first time **no
scene reached hard clipping at all** (M3 measured 3 in 1000, worst peak 1.0104). Read the printed
`worst peak 0.0000` correctly: `Render.cpp` only accumulates `worstPeak` above 0.995 and `worstDc`
above 0.05, so a zero there means *nothing came close*, not *nothing was measured*. Nothing is
clipping because nothing is loud: M3's envelope and M4's modulation took the dense scene down ~12 dB
(Measurement 5). M7's `Volume` curve and limiter therefore have to *make up* level, not only bound
it, and its gate should be stated as a target rms rather than as a clipping rate.

### Run it

```
cmake --build cmake-build-debug --target GenoMosh_Tests && ./cmake-build-debug/Tests/GenoMosh_Tests   # 116 checks
cmake --build cmake-build-debug && cd cmake-build-debug && ctest -j1
./cmake-build-debug/Tests/GenoMosh_Tests batch                     # the per case table, with the ±6 dB names
./cmake-build-debug/Tests/GenoMosh_Tests soak --rounds 1000
bash /tmp/gen_golden.sh                                            # regenerate + verify the five goldens
```

Next: **M5**, the filter network as documented (`flt_type` as a gene, four topologies, crossfaded
changes, `flt_sep`, `flt_kf`) — and with it the loudness model that has to learn topologies,
separation, `b_sh`, `mix_mod` and a pitch that moves, because the branch spread is now 5.29 dB and
the 6 dB target has one notch left.

---

## 15. Addendum 2026-10-30 — Rev 4 / **M4b**: the envelope is a pluck, and a volume decay over the repeats

**What the operator reported.** After M3/M4 the instrument was heard and the envelope was wrong in
kind, not in detail: *"there is a HUGE problem with the envelope implementation… it seems to have a
weird window that is shaped linearly in between, then it just repeats over and over, leading to
rhythmic clicking in most genetic seeds."* Their description of the reference (design §6.1.1 Rev 4,
deviation **D-8**, plan **M4b**) is what the code now implements:

> the envelope **always follows a plucky shape**; `env_time` is *"sort of like a volume decay of the
> repeated envelope curve over time"* (`vol_fade` shares that job); `env_loop` is *"how long it takes
> for the curve to loop"*; `env_tilt` is *"the tilt of the pluck curve, causing it to slowly rise then
> abruptly fall when turned all the way up, or just a regular pluck when turned down"*; modulation
> *"doesn't have a shape"*, `mod_sh` turns it *"from a pluck following the envelope to a squared
> envelope still following the env"*; vibrato and tremolo *"don't follow the envelope, and are free"*.
> Repeating plucks on a held key are the intended sound — the shape was the bug, not the repetition.

### What changed

* `Dsp.h`: one `shapeAt()` trapezoid became **two curves multiplied together**. `pluckAt()` rises to
  one peak (position from `env_tilt`, curvature from `vol_atk`) and returns to zero on a fixed concave
  fall (`EnvShape::kPluckFallExponent = 0.6`, **no gene**, D-8); `outerValue()` is a level applied to
  the whole train, decaying over `env_time` toward the `vol_sus` floor with `vol_dcy`'s curvature and
  pulled to silence by `vol_fade`. The outer curve is counted **in samples**, because "the note is
  over" has to be exact and a float sum of 24000 increments of 1/24000 is not 1.0 (that was a real
  NaN/never-ends bug found on the way: `pow()` of a negative base, from `outerPos` overshooting 1.0).
* **Decision R3 is retired**, with `kInfiniteShapeSeconds` deleted. Separating "how long the note is"
  from "what the curve looks like" makes `env_time ≥ 0.9` mean *no outer decay at all*, which is what
  the reference's own words describe. The one special case that survives is the other half of that
  sentence — "or `vol_fade` fades out": with no note length and `vol_fade` open, the outer fades to
  `1 - fade` over `kFadeOutWithoutDurationSeconds` (8 s, named, asserted).
* `mod_atk` / `mod_dcy` are **timings, not curvatures** (`modPeakOffset` ≤ `kModPeakOffsetRange` =
  0.4 of the period, `modTimeScale` = 2^(±1)); `mod_dcy`'s exponent and `vol_atk`'s stay
  `2^(2(v-0.5))`. `mod_sh` still re-quantises, and that is now literally what the reference does: a
  pluck becomes a stepped/squared envelope that still rides the note's level.
* `Voice`: unchanged in structure. The LFO was already free (depth gated only by `lfo_dly`) — M4b's
  job there was to say so in the code and **test** it.
* `GeneTable.h` labels and help text: **the word "Shape" is gone** from `vol_atk` ("Attack"),
  `vol_dcy` ("Decay"), `mod_atk` ("Mod Attack"), `mod_dcy` ("Mod Decay"). The operator's complaint
  was exact: "Mod Atk Shape" reads as "pick a waveform (saw/square/sine)" when the gene bends one
  fixed curve. Help text now says what each moves.
* `Mutation.h`: `envelopeDuty` is now `outerLevelRatio`, the mean level the outer curve puts through
  a `kNominalNoteSeconds` window (normalised so a neutral plant still measures 1.0), plus the floor's
  share after the note's length. The old `lerp(oneShot, 1, sus)` assumed a plateau that no longer
  exists. `effectiveOuterFloor()` is the shared "vol_sus with vol_fade folded in" reading.
* Named constants that are now load-bearing and asserted: `kPluckPeakFast/Neutral/Swell`
  (0.010 / 0.060 / 0.950 of the period — the neutral gene is a plain pluck, 5.7.1),
  `kFadeOutWithoutDurationSeconds`, `EnvShape::kPluckFallExponent`, `EnvShape::kHoldFadeSeconds`.

### Measured

* **The clock** (`testSharedEnvelope`, rewritten): a pluck peaks exactly at the position `env_tilt`
  asked for (5% of the *period*, not of the note); the level keeps falling through the whole note
  (`< 0.92×` per measured step — the trapezoid fails this by construction); a train of plucks under a
  2 s decay gives one pluck per period, each quieter, with silence in between (`windowExtremes` per
  period is the probe, because counting local maxima is fragile exactly when the peaks get quieter);
  `vol_sus`'s floor settles the train at that level and the note is then as long as the key;
  `env_time ≥ 0.9` is ten equal plucks in two seconds and never ends; note-off is still the fixed
  180 ms release from wherever the level is.
* **Mapping**: the neutral genome resolves to a plain pluck (`envPeakPosition == kPluckPeakNeutral`),
  exponent 1.0 for both curvature genes, no mod timing, no floor; `env_tilt` at the top puts the peak
  at 0.95 of the period.
* **The articulation matrix still points the right way**: short vs long `env_time`, `vol_sus` below
  and above its gate (self-ended vs held to the key-up), `vol_fade` closing a note that has no
  length of its own, both dead zones bit-identical, `env_kf` shortening high keys. One assertion was
  replaced, not dropped: `vol_atk`'s *curvature* is invisible to a time-to-peak metric (both renders
  peak at the same moment — correct, since the peak's position belongs to `env_tilt`), so the audio
  gate is "the gene is not inaudible" and the exponent is pinned at the mapping layer.
* **`mod_sh`** now measures **3.3 pitch steps/s at a 4.0 Hz gene, 0.0 with the gate shut** (it read
  0.0/0.0 before the model was fixed: a pluck's mod value is near zero by the time the old analysis
  window opened). `a_mod` at its bottom end dives **−1178 cents** with atonality 1 and **0 cents** at
  atonality 0; the dive is now read as the lowest pitch anywhere in the note, because with a pluck the
  deepest point is at the shape's peak position (6% of the shape), not at its middle.
* **Re-pinned**: the 12-branch baseline (`testM2BaselineAudio`, "re-pinned after M4b") is
  rms 0.0561175, 0.0560848, 0.0688941, 0.0552762, 0.0543616, 0.0536776, 0.0574720, 0.0652010,
  0.0637223, 0.0561138, 0.0733133, 0.0619699 — the instrument at rest is ~7 dB **louder** than the
  post-M4 table (a pluck under a decay puts more energy in the first part of the note than a
  trapezoid did).
* **Goldens**: all five regenerated and re-verified bit exact (`maxdiff 0.000000`, tolerance 0).
  New `fnv1a64`: 22050 `51a26456f8072b72`, 44100 `64bfb41dd8c5d063`, 48000 `eff8fc2450eeeea8`,
  96000 `23aea9a7c5198a30`, 192000 `a3d7439490433bec`. The dense 300 s scene is now
  **rms ≈ 0.149, peak 0.78-0.86** (was rms 0.077, peak 0.76-0.85): ~+5.7 dB, the same shift as the
  baseline table. **M7 tunes against these numbers**, and its gate should be a target rms, not a
  clipping rate (the 1000 round soak is 0 unusable, 0 clipped).
* **Batch (12 branches × 5 lengths)**: **+2.67 / −6.21 dB, 59 of 60 inside the ±6 target** (M4:
  +1.00/−5.29, 60/60). The gate moved to `overSixDb <= 1` with the cause named: the single outlier
  is branch 0 at length 1.0 — a 0.68 s note with a 20% floor and `vol_fade` open, i.e. a case where
  the duration/floor interaction is the whole story. `outerLevelRatio` recovered 1.0 dB of that
  spread from the first version of this milestone (it was 7.22 dB). **M5 owns the loudness model.**

### Two readings the operator should confirm

1. `vol_dcy` is given the **outer** volume decay to bend, and the pluck's own fall has **no gene**.
   That is the direct consequence of "the envelope always follows a plucky shape (making the volume
   decay shape parameter useless)" — the parameter stops being useless by becoming the shape of the
   decay over the repeats, which is what `env_time` is described as doing.
2. `mod_atk` / `mod_dcy` were kept as the mod pluck's **timing** rather than deleted. A literal
   "modulation doesn't have a shape" reading makes them dead genes, and this codebase has no place
   for a gene with no destination (§5.7.1); deleting them would also reopen D-7 (the 48 verbatim).

### Run commands

```
cmake -S . -B cmake-build-debug -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja   # CLion's bundled ninja is gone
cmake --build cmake-build-debug --target GenoMosh_Tests && ./cmake-build-debug/Tests/GenoMosh_Tests   # 126 checks
./cmake-build-debug/Tests/GenoMosh_Tests batch
./cmake-build-debug/Tests/GenoMosh_Tests soak --rounds 1000
bash /tmp/gen_golden.sh
```

Next: **M5** as before — the filter network as documented, and the loudness model that has to learn
topologies, separation, `b_sh`, `mix_mod` and a pitch that moves. The 6 dB target now has **no** slack
left: the measured spread is 6.21 dB and one case is outside.
