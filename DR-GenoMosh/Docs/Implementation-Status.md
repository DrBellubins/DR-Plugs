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
