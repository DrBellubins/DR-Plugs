# GenoMosh — Implementation Status

*Report format requested by the design document, section 14: what was built,
what was assumed, measured results, open questions.*

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
