# GenoMosh — Rev 3 Alignment Plan

*Plan of record for bringing the code in line with **Revision 3** of
`Docs/Genetic Synth - Design Document for Local LLM.md`.*
*Written 2026-10-29 by auditing `Source/` and `Tests/` line by line against the design document.
Every number in this file was measured from the code, not recalled from a status document.*

**This file supersedes `Docs/Rev2-Alignment-Plan.md`.** That plan is still useful — §2 (its
decision register) and §5 (its "details that will bite") are read repeatedly below — but its
stage list is built on Rev 2's gene *semantics*, roughly eighteen of which Rev 3 overturned.
Where a Rev 2 decision survives Rev 3 it is re-listed here with the same `Dn` number; where
Rev 3 closes or reopens one, that is said explicitly (§4).

**§4 is answered.** The operator replied on 2026-10-29 to every open `R` and every carried `D`.
§4 now records those decisions rather than defaults, and §9 lists only what is genuinely still
open. Two answers came back **different from the default this plan proposed**: R1 is *48 genes
plus a documented extras block* (not "exactly 48"), and R5 is *per-voice wet paths* (not a
shared master bus). R4 came back as *no Rotation*, which closes Rev 2 D12 as originally ruled.
Everything downstream of those three is updated in place below.

---

## 1. Verdict

**The scaffolding is right. The meaning of the parameters is wrong.**

Everything structural in the design document already exists and works: a data-driven gene
table driving both mutation and UI, a normalized `[0,1]` genome, a deterministic splitmix64/FNV
RNG, coherent group directions, monotone `softCompress` boundary handling, threshold-based
"structural traits", guard rails with level compensation that lands 12 far branches inside
~1 dB, a lock-free triple-buffer plant hand-off, a JUCE-free engine that renders offline, and
name-keyed XML state. Design §3, §5.1–5.3 and §10 are substantially met, and §3.1's
"unit-testable core" is load-bearing in the build (`Tests/` links only `Genetic/` + `Engine/`).

The gap is almost entirely **what the genes mean**, plus the curation machinery of §5.7 and the
macro/output surface of §4.1, §7 and §8.

Measured headline (all from `Source/Genetic/GeneTable.h`, counted with a small probe program):

| Quantity | Design (Rev 3) | Code today | Gap |
| --- | --- | --- | --- |
| Genes | 48 (+ 2 documented extras, R1) | **43** (`kNumGenes`, measured) | 10 missing, 3 to delete, 2 to demote to an extras block |
| Genes needing only a rename | — | — | **7** |
| Genes whose behaviour is already as documented | — | — | **7** |
| Genes that exist but **mean something else** | — | — | **24** |
| Gene groups (`enum Group`) | 11 (Env/Vol/Mod/LFO/OscA/Mix/OscB/Filter/Sat/Revb/Out) | **10** (Amp/Mod/Loop/LFO/OscA/OscB/Mix/Filter/Fx/Movement) | restructure |
| Growth-exempt genes (`kFlagNever`) | 5 (`rvb_size`, `adj_*`) | **0** (the flag exists; no gene uses it) | 5 |
| `GeneInfo` fields (§4.3) | 13 | 8 (`id,label,curve,min,max,group,sens,soft,flags`) | `helpText`, `neutral`, `gateLow/High`, `steps`, `colour` |
| `Curve` kinds | Linear/Exp/Squared/Bipolar/**Stepped** | Linear/Exp/Squared/Cubic/Bipolar | **`Stepped` absent** → no 7-step `a_freq`, no 4-step `flt_type` |
| §5.7 curation mechanisms | 9 | **1 partially present** (`applyCouplings`) | 8 |
| Macro parameters (§4.1) | 13 groups + 12 branch lengths | 7 (`volume,velSens,tuning,atonality,effect,wheel,wheelScale`) + 12 branch lengths | `release`, per-branch vol/enable, voice mode/glide, wheel target, tempo sync (`rotation` stays removed — R4) |
| Host-visible gene parameters (§9, like v2) | 48 | **0** | 48 |
| Shared envelope engine (§6.1.1) | one clock, duration gene, curvatures | conventional **ADSR** (`Env`) + a separate loop triangle usable only as a mod source | rewrite |
| Offline test protocol (§13) | `createTest`/`performTest`, WAV, tolerance, spectral, aliasing, articulation | peak/rms/DC/NaN/bit-identical only, **no file output, no spectra** | harness |

So: **the engine skeleton survives this alignment; about half of the parameter model does not.**
The plan below keeps what works and re-semantifies the rest in an order where each milestone
either *unblocks* the next one or *measures* it.

### 1.1 Already aligned — do not churn

Explicitly out of the work list, so nobody "aligns" it by accident:

- `Rng.h` (splitmix64 + Irwin-Hall clamped gaussian + FNV `hash::combine/fromString`) — §10 determinism. Keep.
- `PlantHandoff` triple buffer, `PlantSnapshot` immutability, voice-captures-genome-at-note-on — §3. Keep.
- Coherent per-group directions + per-gene direction + `coherence` blend, `distanceExponent`,
  per-gene sensitivity, `softCompress` (documented deviation **D-1**), monotone and exact at
  length 0 — §5.2. Keep; add only the `pullToNeutral` term (M1).
- Threshold traits (flip once, never flip back) — §5.4. Keep; retarget what they flip (M5/M6).
- Silence re-roll over 3 direction variants + `safetyScore`, `applyCouplings`,
  `loudnessCompensation` (measured ~1 dB) — §5.6. Keep; recalibrate (M3, M5, M7).
- `TrapezoidSVF` (verified against a biquad within 0 dB), `DcBlocker`, `VariableDelay`,
  Hadamard 8-line FDN with mutually-prime lengths, per-line damping, incommensurate length
  modulation, chorus with depth+rate scaled together — §8's recommended architecture. Keep and extend.
- 16-voice pool, release-aware (quietest) stealing, sample-accurate note events, per-block
  `ScopedNoDenormals`, reserved vectors, control-rate (16-sample) target ramping — §10, §6.4. Keep.
- `Source/Genetic` + `Source/Engine` free of JUCE types, and the offline test target that
  proves it — §10. Keep.

---

## 2. Difference catalogue

### 2.1 The gene set (43 → 48)

Renames (7): `balance→osc_mix`, `sat_drive→saturate`, `fx_mix→rvb_mix`, `fx_len→rvb_len`,
`fx_damp→rvb_damp`, `fx_chors→rvb_chor`, `fx_size→rvb_size`.

Deletions (3): `vol_rel` (→ fixed note-off release, deviation **D-5**), `det_spread` +
`drift_amt` (per-voice life is non-gene behaviour, deviation **D-3** — the drift generators
stay, the genes go).

**Extras (2) — decision R1 is "48 + documented extras".** `b_color` and `fx_width` are *not*
deleted: Rev 2 D1's ruling stands, and they move into a clearly-marked extras block appended
**after** the 48, recorded as design deviation **D-7**. The rule adopted with that answer:

- the 48 core genes keep design §4.2's ids, order and page grouping verbatim — the document's
  vocabulary contract is what the UI pages and the DAW parameter list are built from;
- an extra is allowed only if it owns a destination **no documented gene owns**, only if it is
  listed in the deviation register, and never inside §4.2's ordering;
- the UI shows them as a separate "GenoMosh additions" group, and the host parameter list keeps
  the 48 first so they stay navigable.

What qualifies today, and is now written up in design §4.2's "Extras" block: `fx_width` stays (wet mid/side width — `adj_pan` spreads the **dry**
branch signals, which is a different destination), and `b_color` stays as **B's offset from the
shared `a_color` noise shaper**, which also settles design Q-7 (one shared source, plus an
optional per-oscillator offset). `vol_rel`, `det_spread` and `drift_amt` stay deleted: their
behaviour is documented as fixed engine behaviour (D-5, D-3), not as a gene. They may return as
extras only with a new deviation row.

Additions (10): `env_kf`, `vol_fade`, `lfo_dly`, `mix_mod`, `flt_type`, `rvb_atk`,
`adj_bass`, `adj_treb`, `adj_pan`, `adj_clip`.

Result: 38 carried + 10 new = **48 core genes**, matching design §4.2's counts of 16 / 15 / 17
per group, plus the 2-gene extras block = **50 gene slots**.
Memory note: `sizeof(Genome)` is measured 172 B today (43 floats + padding),
`BranchDirection` 192 B, `PlantSnapshot` 7 120 B, **21 360 B across the triple buffer**.
At 50 slots the arithmetic says `Genome` 200 B, `BranchDirection` 216 B × 36 → ≈ 8.0 kB per
snapshot and **≈ 24 kB across the triple buffer**. Not an audio problem, but `PlantView::update` copies a
whole snapshot at 30 Hz (a known weak point since 2026-10-20) and that gets worse — fix the copy
in M0, do not re-derive these numbers later.

### 2.2 Per-gene semantic status

`same` = behaves as Rev 3 describes; `rename` = id/curve change only;
`remap` = the DSP slot exists but the *meaning* is different; `new` = does not exist.

| # | Gene | Status | What the code does now |
| --- | --- | --- | --- |
| 1 | `env_time` | remap | Exp 0.05–16 s, used **only** as the loop period (`loopMod` at `1/loopTime`, `loopEnvOneShot` times). Not the duration of the sound. |
| 2 | `env_loop` | remap | Linear 0–1 as a *crossfade amount* between a one-shot env and a loop triangle, usable only as a `modSource` trait. |
| 3 | `env_tilt` | remap | Bipolar −1..1 → duty of that loop **triangle**, i.e. it skews a modulator, not the amp/mod shape. |
| 4 | `env_kf` | new | — (key follow of duration absent; only filter key follow exists) |
| 5 | `vol_atk` | remap | Exp 0.001–4 s real attack time on a 4-stage `Env`. |
| 6 | `vol_dcy` | remap | Exp 0.01–8 s real decay time. |
| 7 | `vol_sus` | remap | Raw `g[g_vol_sus]` straight to sustain level; **no `<0.5 = no sustain` gate**. |
| 8 | `vol_fade` | new | — |
| 9 | `vol_rel` | delete | A real 4th stage (`Env::Release`, Exp 0.02–12 s). D-5 replaces it with a fixed 180 ms. |
| 10 | `mod_atk` | remap | Real times on a second `Env`. |
| 11 | `mod_dcy` | remap | as above (release = `modDecay * 0.5`). |
| 12 | `mod_sh` | remap | `lerpf(0.3,3.0)` → `pow(modEnv, modShape)`: a **curve exponent**. Not a sample-and-hold rate. |
| 13 | `mod_vel` | same | Scales `a_mod/b_mod/fm_mod/flt_mod` with `VelSens`. Needs the `<0.1 = none` gate. |
| 14 | `lfo_rate` | same | Exp 0.02–30 Hz. (No tempo sync — M9.) |
| 15 | `lfo_amt` | remap | `Curve::Squared` **unipolar** 0..1; no polarity behaviour, no onset delay. |
| 16 | `lfo_bal` | remap | Bipolar −1..1 splitting **pitch vs filter**. Design: vibrato↔**tremolo**, 0.5 = both. |
| 17 | `lfo_dly` | new | — |
| 18 | `a_form` | remap | Four-way equal-power crossfade with breakpoints at 0, ⅓, ⅔, 1 (sine/triangle/saw/pulse). Design's breakpoints are sine 0 / **saw 0.57 / square 0.81** / pulse 1.0, and there is no triangle waypoint. |
| 19 | `a_noise` | same | `Squared` mix into A — but from **white** noise (see `a_color`). |
| 20 | `a_mod` | remap | Mod-env amount on A's **timbre** (`colourA ± 0.35`). Design: bipolar **pitch** modulation — confirmed verbatim by the reference (0.5 = none, 0.0 = max negative, 1.0 = max positive). This directly reverses the 2026-10-21 "the note changes pitch when I play it" fix — see §6.1. |
| 21 | `a_color` | remap | Feeds oscillator **waveform tilt** (`MorphOsc::saw/pulse/triangle` "colour"). Design: **noise colour**, brown→white. `b_color` stays as a documented **extra** giving B an offset from that shared shaper (R1). |
| 22 | `a_freq` | remap | Free Bipolar ±24 st offset on the played note, always ratio-snapped, atonality-ceilinged. Design: **7-step octave grid that is the patch pitch**, and B + both cutoffs link to it. |
| 23 | `fm_mod` | same | Bipolar scaling of the FM index by the mod env. |
| 24 | `fm_amt` | same | A modulated by B (`nextModulated`), index ×3. Needs oversampling/band-limiting (M6) but the routing is right. |
| 25 | `mix_mod` | new | — (mod-env → A/B mix does not exist) |
| 26 | `osc_mix` | rename | `balance`, cos/sin equal-power → **50/50 at 0.5**. Design: 50/50 at ≈0.79, default 0.7937. |
| 27 | `b_form` | remap | as `a_form`. |
| 28 | `b_noise` | same | as `a_noise`. |
| 29 | `b_mod` | remap | as `a_mod` (timbre, not pitch). |
| 30 | `sub_am` | remap | **Sine** sub at `f0/2` (the *note*, not B) added after the mix. Design: true AM of B one octave down. |
| 31 | `b_freq` | remap | Range is right (±24 st, 0.5 = unison) but the ratio snap is unconditional and atonality is a hard clamp; design wants snapping only as an Atonality-blended extension (D-4). |
| 32 | `b_sh` | remap | A **gate + mode selector** (`syncOn/ringOn/holdOn` when `b_sh > 0.02`, mode from the trait). Design: an S&H **rate**, note-synced below 0.5. |
| 33 | `flt_type` | new | Topology comes from `BranchTraits.filterMode` (lp/bp/hp), applied identically to both stages. Not a gene, no parallel topology, no notch. |
| 34 | `flt_q` | remap | `0.5 + 13·v`; cap is `f(flt_freq, sat_drive)` only — design wants it capped per **topology** and `flt_sep` too. |
| 35 | `flt_mod` | same | Bipolar, up to 3 octaves of cutoff modulation. |
| 36 | `flt_sep` | remap | Linear `v·2` octaves, i.e. separation is **always ≥ 0** and 0 is the extreme. Design: Bipolar, **0.5 = no separation**, ±3 oct. |
| 37 | `flt_freq` | remap | Exp **40–18000 Hz absolute**. Design: `cutoff = fA × 2^lerp(−2,+8,v)` — a *tracking* ratio, default 0.75. |
| 38 | `flt_kf` | remap | Unrailed Bipolar × `(note−60)/12` octaves. Design: `smoothstep((v−0.25)/0.5)` soft rails × 1 oct/key. |
| 39 | `saturate` | rename | `sat_drive` is fed to `TrapezoidSVF::setDrive`, i.e. saturation **inside the filter loop**. Design: a per-voice stage *after* the filter, 2× oversampled, DC-blocked, off until ~0.5. |
| 40 | `rvb_mix` | rename | `fx_mix`, scaled by the `Effect` macro. Behavioural match, name+id only. |
| 41 | `rvb_atk` | new | — (no reverb-amount envelope anywhere) |
| 42 | `rvb_len` | rename | `fx_len`, Exp 0.05–12 s RT60 → design range 0.2–12 s. |
| 43 | `rvb_damp` | rename | `fx_damp`, one-pole per line (18 kHz→700 Hz). Match. |
| 44 | `rvb_chor` | rename | `fx_chors`, depth+rate together, chorus *before* the FDN. Design puts the taps **inside/across the tail**. |
| 45 | `rvb_size` | rename+flag | `fx_size` → delay-line scale 0.25–1.0. Must become **`kFlagNever`**. |
| 46 | `adj_bass` | new | — (no shelves) |
| 47 | `adj_treb` | new | — |
| 48 | `adj_pan` | new | Pan is a per-voice random ±0.30 rad. Design: **branch-position** spread scaled by `adj_pan` and reduced by `Effect`. |
| — | `adj_clip` | new | Master `softClip(x · volume) · 0.95` is unconditional, not a driven, documented clipper. |
| — | `det_spread`, `drift_amt` | delete | Present today; become named constants (D-3). |
| — | `b_color`, `fx_width` | **extra** | Present today. Kept in the extras block (R1): B's colour offset, and wet mid/side width. Neither duplicates a documented gene's destination. |

### 2.3 Gene metadata and curves (§4.3)

`GeneInfo` has no `helpText`, no `neutral`, no `gateLow/gateHigh`, no `steps`, no `colour`;
`Curve::Stepped` does not exist; `kFlagNever` is declared but **used by no gene** (measured:
0 never, 5 `kFlagPitch`, 2 `kFlagRatio`, 38 untagged); there is no `kFlagLinked` or
`kFlagStructural`. Group enum is the Rev 1 grouping (Amp/Mod/Loop/LFO/OscA/OscB/Mix/Filter/
Fx/Movement), not the document's. `GeneGrid::resized` splits its two UI columns at
`GrpCount/2` = 5, which silently mis-pages whenever the grouping changes.

The nine curation mechanisms of §5.7, one line each:

| §5.7 | Mechanism | In code? |
| --- | --- | --- |
| 1 | neutral at 0.5 | **No** — no `neutral` field; defaults are ad-hoc per gene; `randomizeGenome` biases toward "instrument-like" instead of neutral. |
| 2 | linking (`a_freq`→B + both cutoffs, `Effect`→mix+pan, `VelSens`×`mod_vel`) | **Partially** — only the last one. No `kFlagLinked`, no resolved-once links. |
| 3 | activation gates / dead zones | **No** — incidental `> 0.02` / `> 0.001` guards exist in `Voice::render`, none of the documented ones. |
| 4 | soft rails (`flt_kf` <0.25 / >0.75) | **No** — `softCompress` is growth-space, not per-gene rails. |
| 5 | deliberate quantisation (7-step `a_freq`, 4-step `flt_type`, note-synced `b_sh`) | **No** — replaced by continuous ratio snapping. |
| 6 | growth-exemption | **No** — flag present, zero genes set it. |
| 7 | non-linear musical curves (`osc_mix` 0.79, `Volume (2v)^log2(10)`, `Release 50 ms×400^v`) | **No** — none of the three. |
| 8 | couplings as one-line generation-time edits | **Yes, in kind** — `applyCouplings`/`applyAtonality` are pure, unit-tested genome-space code. But atonality is a hard **ceiling on 5 pitch-tagged genes + unconditional ratio snapping**, not `converge(a_mod), converge(b_mod)`. |
| 9 | identity as data (base-32 seed codes, word-list names) | **No** — `seedId` is a `juce::String` of an int in XML; no code, no names. |

### 2.4 Envelope and modulation (§6.1, §6.1.1, §6.2) — the largest single gap

- `Voice` has a conventional **ADSR** (`Env`: Attack→Decay→Sustain→Release) with `vol_atk/
  vol_dcy/vol_sus/vol_rel` as real times. There is **no shared clock**, no duration gene, no
  loop period, no curvature exponents, no key-follow of duration.
- `env_time/env_loop/env_tilt` today drive a *separate* free-running triangle (`loopMod`) plus a
  one-shot env, crossfaded by `env_loop`, selectable as a `modSource`. Design has no such source.
- Note-off: `Env::gateOff()` → a per-patch release time. Design D-5: stop looping, fixed 180 ms.
- LFO: → pitch (× `0.5−0.5·lfo_bal`) and → **filter** (× `0.5+0.5·lfo_bal`). Design: pitch +
  **volume** (tremolo) at one rate, equal-power, `lfo_amt<0.5` = **opposite phase on A and B**,
  `lfo_dly` onset fade. There is no tremolo path at all (the amp is envelope-only).
- The LFO's *shape* is randomised per voice (`lfo.setShape(rng.integer(3))`: sine/triangle/S&H).
  With two documented S&H sources, a random S&H "LFO" muddies what the `mod_sh`/`b_sh` genes mean.
- `mod_sh` is an envelope curve exponent; `b_sh` gates three oscillator-B modes. Neither is the
  documented S&H rate, and note-synced S&H on B does not exist.
- `modSource` (mod env / loop env / LFO) is a structural trait. With the sources fixed per §6.2,
  it becomes redundant (Rev 2 plan §5.6 already argued this; Rev 3 confirms it).
- Missing destinations: `mix_mod` (mod env → A/B mix). Present but disputed: `a_mod`/`b_mod`
  (see §6.1 below).
- Present and blessed by the doc as our extension: two `Drift` random walks (pitch + cutoff),
  per-voice random detune/phase/pan, per-voice LFO shape/phase. Keep — but as non-gene
  behaviour (D-3), since `drift_amt`/`det_spread` are deleted. Their amount becomes a constant
  until M10 decides otherwise.

### 2.5 Voice and filter (§7)

- Filter: **two cascaded** SVF stages, both in the mode chosen by a trait (lp/bp/hp). No
  parallel topology (the reference's *default*), no notch, no topology crossfade
  (`p.filterMode == 0 ? 1.0f : 0.0f` is computed once per block from an integer).
- Cutoff is absolute Hz and does not track the keyboard — the single change that most affects
  whether branches keep their character across octaves.
- FM is phase modulation at a fixed index, **no oversampling**; sub is a plain sine; noise is
  white with no colour shaping; the morph is a crossfade of four shapes rather than a band-limited
  morph at the documented breakpoints. `Implementation-Status` §4 already admits 2, 3 and 4.
- Structural traits are not crossfaded in the voice (admitted). Voice stealing calls
  `release()` and immediately starts on the stolen voice — no fade (§7 asks for a real fade).

### 2.6 Output stage, macros, reverb (§4.1, §8, §8.1)

- No `adj_bass/adj_treb` shelves, no `adj_pan`, no separate `adj_clip`. The only output
  nonlinearity is `softClip(x·volume)·0.95`.
- `Volume` = `raw · 1.15` into that clipper. Design: `gain = (2v)^log2(10)` (20 dB/doubling,
  0 dB at 0.5) into a **saturating limiter** whose drive comes from the same value.
- `Tuning` parameter spans ±100 cents. Design: **±12 semitones**, plus `a_freq`'s octave grid.
- `Effect` scales only the wet path (`fx_mix × lerp(0.25,1.25)`). Design: mix **and** pan spread.
- `Release` macro: absent — no global release time and no reverb gate.
- Wheel: `d = branchLength · wheel · wheelScale`, wheel parameter defaults to **1.0** (the
  "default the plant open" workaround), and CC1 overrides the host parameter at runtime.
  Design: `lerp(branchLength, 1.0, clamp01(wheel · wheelScale))`, default wheel **0**, which
  makes the workaround unnecessary. No `Wheel Target`, no `Tempo Sync`, no `Voice Mode`/glide.
- `Rotation`: removed from the code (2026-10-21, Rev 2 plan D12) and **stays removed** (R4,
  2026-10-29). Rev 3's §4.1/§5.3/§12 text mentioning it has been amended out; the design doc no
  longer lists it, and there is no rotation handle.
- Reverb: FDN shape matches §8's recommendation, but there is no `rvb_atk` bloom, no gating
  from `Release`, no per-branch enable/trim, and the wet path is a single shared bus — R5 makes
  it **per voice**, with `fx_width` kept as an extra on that per-voice wet signal.
- Branch identity/planting: `plantBranch` writes the grown genome into the seed and resets all
  twelve lengths to `kDefaultBranchLength = 0.45`, leaving `seedId` alone. Design §5.8: set all
  twelve lengths to that branch's length **and** `seedId += branchIndex`.
- `branchForNote(note) = note % 12` — correct pitch-class mapping, but no rotation offset and
  no root-note reference.

### 2.7 Architecture and state (§3, §10, §11)

Aligned: immutable snapshot + triple buffer; no allocation on the audio thread in the render path
(vectors reserved at `prepare`); deterministic PRNG; name-keyed XML with `version` and
`mutationVersion` attributes; JUCE-free core; sample-rate-derived coefficients throughout.

Diverges:
- Branch genomes are resolved **on the audio thread at every note-on** (`startVoice` →
  `resolveBranchGenome`: up to 3 full-genome `applyDistance` + `applyCouplings` +
  `safetyScore` passes, then `applyAtonality`). Design §3 asks for pre-resolved branch genomes;
  §10 blesses the hybrid — "a per-branch cache keyed by (snapshot revision, quantised distance)".
  The current code is the uncached version of that hybrid. Add the cache (M8); keep directions
  (Rev 2 D14 stands).
- `float` throughout; §10 recommends double for long delay lines and phase accumulators.
- Version axes: only `version` + `mutationVersion`; design wants `formatVersion`, `modelVersion`
  and `mutationVersion` separated, plus a migration/`legacyMode` story. The 43→48 delta makes old
  states **unreadable** (R11, decided: refuse to load with a warning; no fake migration).
- No seed codes (§5.7.9 / §11 base-32 over `23456789abcdefghjkmnopqrstuvwxyz`), no word-list
  patch names, one program, no factory bank, no `layers`/`Bulb Mode` (correctly out of scope).

### 2.8 Host / MIDI (§9)

Absent entirely: pitch bend and `PitchWheelRange`, sustain pedal, mono/legato + glide, tempo
information, MIDI receive/transmit channel config, `VelocityCurve` (velocity is linear today),
program changes, MIDI-learn, VST3 `IMidiMapping`/`IMidiLearn`, and the 48 gene parameters.
Present: note on/off, CC1 (wheel), all-notes-off, automatable branch lengths.

### 2.9 Testing and tooling (§13)

The existing harness is good and covers: curve round-trips, ratio-snap direction, atonality
ceiling, pitch stability, determinism, exact-at-length-0, monotone deviation, guard rails
(720/720 usable branches), SVF accuracy, per-branch peak/rms/DC, bit-identical re-renders,
multiple sample rates, note bookkeeping.

Missing: any file output (**no WAV writer**), the `createTest/performTest {seed,time,sr,density,
file,tolerance}` protocol, the 12 branches × 5 lengths batch, spectral centroid/flatness,
aliasing estimate, **envelope-articulation check**, the 10 000-round `massiveAudioTest` soak, and
a loudness target of ±6 dB (the current gate is ±12 dB). Two tests assert *Rev 1 semantics* and
must be re-based, not just re-run: `testPitchStability` asserts that the mod env never touches
pitch (design says `a_mod`/`b_mod` are pitch mod), and `safetyScore`/`applyCouplings` reference
genes that will not exist (`vol_rel`-era times, `sat_drive`, absolute `flt_freq`).

### 2.10 UI (§12)

`PlantView` (ring of 12 pitch classes, drag to grow, click to play, right-click to plant, idle
sway, hue from the hash) and `GeneGrid` (43 sliders, group headers, two columns split at
`GrpCount/2`) exist. Missing from §12: per-group gene pages with the document's page split,
group colours, per-gene help text, **grown-vs-base value display**, response plots, the extras
section, mod-wheel arc, per-branch trim/enable, and a raw-0..1 readout (§3.1: the gene UI shows
the raw float). The rotation handle stays out (R4).

---

## 3. What this costs

Rough sizing, in the order of the milestones, counting only files that must change:

| Milestone | Files | Nature |
| --- | --- | --- |
| M0 table | `GeneTable.h/.cpp`, `GeneGrid.*`, `PlantView.*`, `PluginProcessor` state | mechanical but wide |
| M1 mapping | `Curves.h`, `VoiceParams.*`, `Mutation.*` (neutral pull, `kFlagNever`) | small, dense, all pure |
| M2 harness | `Tests/EngineTests.cpp`, new `Tests/Render.*`, `Tests/CMakeLists.txt` | additive |
| M3 envelope | `Dsp.h` (new `SharedEnv`), `Voice.*`, `VoiceParams.*`, `Mutation.cpp` (score), tests | the core rewrite |
| M4 modulation | `Voice.*`, `Dsp.h`, `VoiceParams.*`, tests | high risk (pitch) |
| M5 filter | `Voice.*`, `Dsp.h` (topologies), `VoiceParams.*`, `Mutation.cpp` (Q caps), tests | medium |
| M6 voice quality | `Oscillator.h`, `Voice.*`, `Dsp.h` (oversampled sat, shelves later), `Synth.cpp` (steal fade) | medium |
| M7 output/reverb | `Voice.*` (per-voice wet path), `MasterFx.*`, `Synth.cpp`, `VoiceParams.*`, `PluginProcessor` params | medium–large: the reverb moves into the voice (R5) |
| M8 growth/curation | `PlantEngine.*`, `Mutation.*`, `Curves.h`, `PluginProcessor.*`, tests | small, all testable |
| M9 host/state | `PluginProcessor.*`, VST3 wrapper work, `GeneTable` metadata | wide, low-risk |
| M10 tuning | `GeneTable.h` (sensitivities, soft ranges, gates) + harness | iterative |
| M11 UI | `UI/*` | additive |

---

## 4. Decision register — **answered 2026-10-29**

`R` = new here. `D` = inherited from Rev 2 plan §2. **Every row below is a decision, not a
default**: the operator answered all of them on 2026-10-29, and the verbatim answers are kept in
`Docs/Rev3-ChangeLog.md` §5. Three answers came back **against the default this plan proposed**
(R1, R4, R5); §2 and §5 are already updated to match, and the design document was amended where
an answer overrode its text. Only design §15 Q-5 is still open, and it keeps its documented
default.

| # | Question | **Decision** | Notes |
| --- | --- | --- | --- |
| R1 | Gene set: exactly 48, or 48 + documented extras? | **48 core + documented extras.** The 48 keep design §4.2's ids, order and page grouping verbatim. `b_color` (B's *offset* from the shared `a_color` noise shaper) and `fx_width` (wet mid/side width) survive in a clearly-marked extras block **after** them — 50 slots, recorded as design deviation **D-7**. `vol_rel`, `det_spread`, `drift_amt` stay deleted: their behaviour is documented engine behaviour (D-5, D-3), not a gene. | **Against this plan's old default.** Rev 2 D1/D2 reinstated. Test: the first 48 table entries match §4.2 exactly, the extras block is separate (§2.1, M0). |
| R2 | Are `a_mod`/`b_mod` really pitch modulation? | **Yes**, exactly as the reference documents: *"a_mod is bipolar and determines the pitch modulation amount from the Modulation Envelope. 0.5 means no pitch modulation. 0.0 is the maximum negative, and 1.0 is the maximum positive modulation."* — **and the same applies to `b_mod`**. So: bipolar mapping, `0.5` = that modulator is silent, no dead zone, full swing at both ends. Keep our depth ceiling and generation-time `converge()`, and rewrite `testPitchStability` (§6.1). | Confirms the default. The 2026-10-21 fix stays deliberately undone — it removed *unbounded* pitch mod, not pitch mod. |
| R3 | Envelope shape times: fractions of the cycle, or absolute seconds? | **Fractions of `env_time`**, which *is* the duration: *"env_time controls the total duration of the sound. Above 0.9, the Envelope will have infinite duration, stopped only by releasing the key or vol_fade fades out."* | Confirms the default and settles the ∞ case: `env_time ≥ 0.9` ⇒ duration ∞, ended by key-up or `vol_fade`. The shape still needs a time base in that case (§6.2, M3). |
| R4 | Restore `Rotation`? | **No.** No Rotation macro, no rotation term in the note→branch map, no rotation handle: `branchForNote = (note − rootNote) mod 12`. | **Against this plan's old default.** Rev 2 D12's ruling stands; design §4.1/§5.3/§9/§12/§14 have been amended to drop it. |
| R5 | Where does `rvb_atk` live? | **Per-voice wet paths**, with a heavily optimized reverb — per-voice tail quality is not needed because the wet signals sum. Each voice owns its `rvb_*` instance; `rvb_atk` is that voice's wet-amount bloom; the `Release` gate is per-voice too; master is sum → shelves → `Volume` → limiter/`adj_clip`. | **Against this plan's old default** (a `Synth`-level wet-send bloom). Closes design **Q-9**; forces Q-8's answer (shelves after the reverb). Cost is the risk: §6.5 and M7's CPU gate. |
| R6 | Does the LFO keep a filter route? | **Not as a fixed route.** Only as a rare structural routing (§6.3, deviation D-6). | Rev 2 D6 stands, labelled as an extension. |
| R7 | Does Atonality converge only `a_mod`/`b_mod`? | **Yes** — `converge(x, at)` at generation time — plus the D-4 ratio snap on `b_freq` blended by the same value. **Drop the 5-gene hard ceiling.** | Confirms the default; closes design Q-6. Rev 2 D7 (do not suppress tremolo) stands. |
| R8 | Keep resolving directions on the audio thread? | **Yes**, with the §10 quantised per-branch cache. | Rev 2 D14 stands; only the cache is missing (M8). |
| R9 | Are the 48 host parameters a copy of the genome, or its store? | **Its store.** Gene parameters are `AudioParameterFloat`s in a `Genome` unit whose listener writes the seed genome; XML stays the patch serialisation only. | One source of truth (M9, §6.7). |
| R10 | Structural traits vs stepped genes | **Stepped genes are authoritative.** A trait may offset a stepped gene by ±1 step and swap a destination's source, never replace a gene. | Rev 2 D8 stands, refined; `filterMode` trait is removed in favour of the `flt_type` gene. |
| R11 | Old (43-gene) saved states | **Refuse to load, warn, keep the instance at a fresh plant.** Add `modelVersion`; **no fake migration**. | Replaces Rev 2 D15. Design §11's "converter" line is amended to this. |
| R12 | Per-branch pan in chords | Implement as documented, with a variance-bounding coupling **if chords smear** (M10 listens first). | Rev 2 plan §5.4 / question 13. |
| D5 | Note-off behaviour | Unchanged: stop looping, **fixed 180 ms** release, named constant. | Design deviation D-5. |
| D9 | Re-resolve held notes with a ~50 ms crossfade? | **No** (as before). It is the only way to see §5.4's crossfades, so it becomes an M6 **debug-mode affordance**, not default behaviour. | Answered under the label "D6" in the reply; it is the Rev-2 **D9** held-note question. Rev 2 D6 itself is the R6 row above. |
| D13 | `softCompress` | Keep. | Design deviation D-1. |
| D16 | 16 voices | Keep, **with a real fade on stolen voices** (M6). | |
| D3, D4, D8, D10, D11, D12, D15 | | **Closed or reopened by Rev 3**: D3 (`env_loop` = period) and D8 (`flt_type` is a gene) are settled by the design; D4, D10, D11 are **overturned** (curvature, S&H rate, `converge`); **D12 is closed as originally ruled — no Rotation (R4)**; D15 is replaced by R11. | |

**Still open:** design §15 Q-5, saturation placement — the reference says per-voice `saturate` +
master `adj_clip`, `Docs/Rev2-Answers.md` A2 says the opposite. Implement the reference's
version (M6) and mark the code with the question; it is one graph edge either way. Q-1 (layers /
Bulb Mode), Q-3 (`Wheel Target`'s 9 values), Q-4 (`a_form` PWM), Q-10 (formats) stay unanswered
and off the critical path. Q-7 is answered by R1 (shared shaper + a documented `b_color`
offset), Q-9 by R5, and Q-8 follows from R5: with a per-voice tail the shelves can only sit
after the reverb. The per-voice drift/detune amounts stay named constants (D-3), not a macro —
M10 may revisit that with measurements.

---

## 5. The plan

Twelve milestones, in dependency order. Each has a **gate** that is a command you can run and
read, and an **unlocks** line, because that is what makes the order non-arbitrary.

```
M0 table ─┬─> M1 mapping ─┬─> M2 harness (baseline!) ─┬─> M3 shared envelope ─┬─> M4 modulation
          │               │                            │                        │
          └─ (UI still compiles)                      └─> M5 filter ───────────┴─> M6 voice quality
                                                                          └────> M7 output + reverb
M4 + M5 + M7 ──> M8 growth & curation ──> M9 host/state ──> M10 tuning ──> M11 UI
```

### M0 — The gene table becomes the 48 documented genes

*Why first: every other milestone reads this table. Doing it first also makes the rest of the
work compile, and it is the only milestone whose failures are compile-time rather than audible.*

- Rewrite `GENO_MOSH_GENE_LIST` to design §4.2's 48 ids, in the document's order and groups
  (16/15/17), **then** the 2-gene extras block (`b_color`, `fx_width`) in its own `GrpExtras`
  group (R1). Apply the 7 renames, 3 deletions, 10 additions and 2 demotions from §2.1.
- Extend `GeneInfo` with `helpText`, `neutral`, `gateLow`, `gateHigh`, `steps`, `colour`; add
  `kFlagLinked`, `kFlagStructural`; add `Curve::Stepped`; renumber `enum Group` to the
  document's 11 groups **plus** `GrpExtras`, and set `kFlagNever` on `rvb_size` + the four `adj_*`.
- Write one sentence of `helpText` per gene (design §12; they exist to be written, not invented).
- Provisional wiring so everything still builds: deleted genes become named constants in
  `VoiceParams` (`kNoteOffReleaseSeconds = 0.18f`, per-voice drift/detune amounts, fixed release
  for the amp), new genes resolve to their documented **neutral** value (i.e. audibly nothing),
  old group assumptions in `GeneGrid::resized` and `Mutation::buildBranchDirection` are removed
  (`GrpCount/2` column split, group-direction arrays).
- `modelVersion` bump in `getStateInformation`; a `modelVersion` mismatch in `setStateInformation`
  is refused with a one-line warning (R11).
- Fix the `PlantView::update` whole-snapshot copy (hue/hash/revision only) — 21 kB at 30 Hz today.
- Add `testTableMatchesDoc`: the **first 48** entries' ids, order, group, curve and `steps` for
  `a_freq`(7)/`flt_type`(4) match §4.2 verbatim; the extras block is separate and marked; every
  gene's `neutral`/`gateLow/High` is documented or explicitly `0,1`.

**Gate:** builds; 48 core entries in document order + a marked extras block (50 slots);
`testTableMatchesDoc` green; new sound = old sound for every gene whose behaviour did not change
(spot-check by rendering M0's baseline vs `1f245e9`'s for the 7 `same` genes); measured
`sizeof`s printed once and recorded (expected ≈ 200 B `Genome`, ≈ 8 kB per snapshot).
**Unlocks:** M1 (neutral/gates/steps are only meaningful once the table carries them) and M2.

**Status: landed 2026-10-30.** The VST3 target and the offline test target build with no new
warnings; `testTableMatchesDoc` is green (48 ids, order, group, curve and `steps` spelled out
from §4.2, the two extras at slots 48/49, every gene carrying `helpText`/`neutral`/gates,
exactly five `kFlagNever`); `testGrowthExempt` was added as the test for the five exempt genes
and is green; sizes measured 200 B `Genome`, 216 B `BranchDirection`, 8 008 B `PlantSnapshot`,
24 024 B across the triple buffer; `PlantView::update` keeps twelve 24-byte drawing fingerprints
and refreshes on revision change only.

The "new sound = old sound" half of the gate had to be done with the harness's per-branch
peak/rms/dc table, because there is no WAV writer until M2; both tables are recorded in
`Implementation-Status.md` §10. Six of the seven `same` genes are bit-for-bit unchanged in
curve and range (`mod_vel`, `lfo_rate`, `a_noise`, `b_noise`, `fm_mod`, `flt_mod`); `fm_amt`
and `saturate` keep their old depth ranges under the curves §4.2 names. The plant as a whole
came out **+2.4 dB with a 4× tighter branch spread** (2.7 dB → 0.6 dB) and DC an order of
magnitude up but 10× inside its gate — expected consequences of `flt_freq` becoming the tracking
cutoff and of the soft ranges being re-centred on the new neutrals, and the new numbers are the
baseline M7 tunes against. Two things beyond the M0 bullet list also landed because they were
free: `randomizeGenome` leaves growth-exempt genes at neutral, and the atonality ceiling reads
`neutral` from the table instead of inferring it from the curve (both were M1 items).

Two deliberate departures from the M0 bullet list. (1) The coherent **group direction** arrays in
`buildBranchDirection` are *kept*: they are indexed by `GeneInfo::group`, so they carry no group
assumption, and §1.1 lists coherent groups under "already aligned — do not churn". What the M0
bullet actually targets was `GeneGrid`'s `GrpCount/2` column split, which is gone. (2) Flagging
`a_mod`/`b_mod` as `kFlagPitch` (§4.2's own flag column) means the existing generation-time
ceiling already converges them toward neutral as Atonality closes — which is R7's requirement,
pulled forward by the table rather than by M4. It is also what makes `testPitchStability`'s
current assertion vacuous at atonality 0.

### M1 — One mapping layer: neutral, gates, steps, links

*Why here: §5.7's mechanisms are pure gene-space arithmetic. Doing them before any DSP change
means they can be unit-tested against numbers, which is far cheaper than testing them by ear.*

- `Curves.h`: `Curve::Stepped` (with `steps`) both ways; `gated()` (`gateLow/gateHigh`
  activation, used by `vol_sus`, `vol_fade`, `rvb_atk`, `lfo_dly`, `mod_vel`, `mod_sh`);
  `softRails(v, lo, hi)` for §5.7.4 (`flt_kf`); `neutralOf(gene)`.
- `VoiceParams.cpp` becomes the **only** place links are resolved (`kFlagLinked`): `a_freq` →
  patch octave, B's pitch offset, both filter cutoffs; `Effect` → wet + pan; `VelSens × mod_vel`.
  Delete the ad-hoc link arithmetic in `Voice::updateControlTargets`.
- `osc_mix` warp so 0.79 = equal power; default `0.7937`; `flt_type`/`a_freq` stepped mapping.
- `Mutation`: **skip genes with `kFlagNever`** in `buildBranchDirection` (already handled if the
  flag is set — verify with a test) and add the §5.2 `pullToNeutral` term using `gi.neutral`.
- `randomizeGenome` = gaussian around each gene's neutral, reflected at the rails, then
  `softMin/softMax` clamped (§11), replacing today's uniform-in-soft-range draw.
- Delete `snapToRatio`'s unconditional application (it returns with Atonality in M8/R7).

**Gate:** round-trip for every gene including stepped ones; "a gene at `neutral` changes nothing
audible" (render-with-neutral vs render-with-default per gene, assert RMS delta < 0.5 dB);
`kFlagNever` genes provably identical across branch lengths 0…1; deterministic across runs.
**Unlocks:** M2 can measure meaning instead of accidents, and M3's articulation metrics are sane.

**Status: landed 2026-10-30.** Round-trip is green for all 50 slots including both stepped
genes; the neutral check, the six gates (at both ends), the soft rails, the mix warp, the
seed distribution, `pullToNeutral`, and the growth-exempt identity are all tests now — 28 →
**44 checks**, all green, VST3 and Standalone build with no new warnings. Full record and
measurements: `Implementation-Status.md` §11.

How the middle clause of the gate was read, since it is the ambiguous one: per-gene RMS deltas
against a *random* genome would fail for genes that are legitimately loud (`flt_freq`,
`a_freq`), and design §14 phase 0 states the same requirement as "neutral value produces the
same sound as the gene at rest". So it is asserted in two exact forms: (1) with all 50 genes
at `neutral`, every destination of `VoiceParams` reads its identity value (0 offset, no
modulation, no separation, no key follow, no drive, no dB, no spread, sustain 0, S&H off,
equal-power mix, quietest Q, wide-open tracking cutoff, default topology step) — stronger than
a 0.5 dB audio tolerance and it names the destination that gets it wrong; and (2) in audio, on
the one gate whose destination already exists: a gene inside its dead zone renders **bit
identically** to the same gene at neutral (`rms`/`peak` compared with `==`), while opening
that gate is +1.36 dB, and the neutral plant must itself be audible or the comparison is
between silences. If that reading is wrong, the fix belongs in `testNeutralGateIsAudible`.

Three things the bullet list did not anticipate, all now documented in code:

* **The lower gate rail belongs to the dead zone, the upper one to the gene.** Every gated
gene has `gateLow == neutral`, so with the boundary "open" the neutral genome would run an
8 Hz sample-and-hold (`mod_sh`) on a plant that §5.7.1 says is at rest. `gateOpen()` is
therefore `v > gateLow && v <= gateHigh`.
* **`gated()` rescales the live part of a range across all of [0,1]** rather than only
clamping, because that is what §6.1.1's `vol_sus = max(0, 2(v−0.5))` is. Consequence for
later milestones: no destination may apply a gate a second time, and gates are never written
into genomes. `applyCouplings`/`safetyScore` still reason about `vol_sus` and friends in raw
gene space (plan §6.3) — M3 must not assume they see gated values.
* **`pullToNeutral` forced the `kFlagNever` skip into `applyDistance` too.** A zero direction
was enough while displacement was only `dir · d`; the neutral term is non-zero on its own.
`testGrowthExempt` and `testPullToNeutral` now pin both halves.

One thing M1 surfaced that needs an operator nod (it is not a code problem): design §4.2 gives
`b_freq`'s factory value as **0.6875** — +9 st on our curve — while its *neutral* is unison.
Two different facts; the table stores only the neutral. Suggested: keep `neutral = 0.5`, add a
`default` when M9 builds the parameter list. Nothing in M1 depends on the distinction.

The plant came out **2.0 dB quieter with a 0.35 dB branch spread** (was 0.57), and DC dropped
another 5× — from the gated sustain, `flt_q` finally going through its `Squared` curve,
`mod_sh` no longer driving the mod-envelope curve (its old use as a shape exponent is pinned
to the identity until M4 builds its real destination), and seeds drawn nearer the middle of
every range. Usable branches are still 720/720. Those are the numbers M2 records as the
baseline and M7 tunes against.

### M2 — The §13 harness, before the sound changes

*Why here: `Implementation-Status` §4 admits the gene ranges have never been measured. Rev 2's
plan made the same argument for putting tooling before sound work, and Rev 3 §13 makes it a
requirement. A baseline captured *before* M3 is the only way to know later what actually changed.*

- `Tests/Render.{h,cpp}`: a small WAV writer (correct `WAVE_FORMAT_PCM` header — byte-accurate
  frame count and channel/frame size, since a wrong header is the usual way a green test suite
  turns out to have written silence) and `renderScene(seed, seconds, sampleRate,
  notesPerSecond)` → the document's `density` semantics: a seeded note generator.
- `createTest` / `performTest {seed, time, sr, density, file, tolerance}` and
  `massiveAudioTest {rounds}` in the test binary; sample rates 22 050 / 44 100 / 48 000 / 96 000
  / 192 000; batch mode = 12 branches × lengths {0, 0.25, 0.5, 0.75, 1.0}.
- Metrics per render: peak, RMS, loudness vs seed (**±6 dB**, tightening today's ±12 dB),
  silence, NaN/Inf, DC, **spectral centroid and flatness**, **aliasing estimate** (energy above
  the expected harmonic series at high notes), and an **envelope-articulation estimate**
  (attack/hold/release times read off the rendered amplitude).
- Golden files: commit nothing large; generate the golden set in CI/locally, keep the WAVs out of
  git, keep hashes in a text file. Record the **M1-era baseline table** in
  `Implementation-Status.md` and expect it to be invalidated by M3–M7 — that is the point.

**Gate:** `GenoMosh_Tests createTest … performTest …` round-trips bit-exactly at 5 sample rates;
`tolerance` is honoured; the 12×5 batch runs and prints metrics for every case; soak of ≥ 1 000
rounds is clean (no NaN, no silence, no runaway DC).
**Unlocks:** measurable gates for M3–M7 and the M10 tuning loop.

**Status: landed 2026-10-30.** `Tests/Render.{h,cpp}` is the protocol; `Tests/EngineTests.cpp`
is the assertions and the command line; **44 → 93 checks**, `ctest -j1` green in 45 s, and no
file under `Source/` changed. The gate as written is met: `createTest`/`performTest` round-trip
**bit exactly (max diff 0.0, 0 LSB) at all five sample rates**, `tolerance` is honoured in both
directions (a one-byte change in a golden fails at tolerance 0 and passes at 0.01; a file whose
header says 22 050 is refused by a 44 100 spec), the 12×5 batch prints the full metric set for
every case, and `soak --rounds 1000` is clean of NaN, silence and DC. The documented golden set
(300/300/300/150/60 s at the five rates, seed 1, density 10) is generated into `Tests/Golden/`
with hash sidecars committed and audio gitignored; the full record is
`Implementation-Status.md` §12.

**Two findings, and they matter more than the tooling.**

1. **§10 and §11's audio tables measured the seed, not the branches.** The old harness built a
   `PlayState`, filled it in, and never called `synth.setPlayState()`, so every "per branch"
   number in this plan's earlier milestones was rendered at the engine's default state: branch
   lengths 0, wheel closed, default macros. "Branch spread 0.57 → 0.35 dB" is a **semitone**
   spread. Nothing else in M0/M1 depends on it — those gates are gene-space or single-render —
   but **M7 tunes against §12's numbers, not §11's**. `testM1BaselineAudio` reproduces those
   twelve rms values through the new harness to **6.15e-07 relative** (keeping the old call's
   stray sample offset of 8, which is part of what produced them), which is simultaneously the
   proof that M2 changed no sample and the record of what the old harness did.
2. **Branch-vs-seed loudness, measured for the first time: the ±6 dB target is not met.**
   49 of 60 cases are inside it; growth is tracked to 0.1 dB up to length 0.25, and from 0.5
   a *minority* of branches fall away by 17–24 dB rather than drifting (worst −23.6 dB,
   loudest +1.64 dB). All eleven offenders are the same failure, and three metrics agree on
   which: centroid 10–22 kHz against their seeds' 378–733 Hz, off-grid energy 0.26–0.92
   against ≤0.03, hold level −7 to −19.5 dB against −5.7 to −7.0. They grew into a shrill
   non-sustaining blip instead of a note. That is plan §6.3 arriving on schedule:
   `loudnessCompensation`/`safetyScore` reason in raw gene space while `vol_sus` is gated, and
   nothing compensates a note that stopped sustaining. **The eleven are M3's**, and the batch
   gate is written as the measured distribution with the §13 target named as a constant, so M3
   improves something visible instead of a tolerance being widened.

Three things the bullet list did not anticipate:

* **The metrics need their own tests before any engine number means anything.**
  `testMetricsOnKnownSignals` checks each one against a signal built from its answer (a 440 Hz
  tone, white noise, a 220 Hz harmonic stack with and without an inharmonic partial, a sampled
  26.15 kHz tone that lands on 21.85 kHz, and three hand-written amplitude envelopes). Getting
  that green corrected two estimator designs: an autocorrelation's *largest* peak is an
  octave-low trap for harmonic-rich sound (use the first strong peak, then refine the spacing
  on the spectrum), and a lone tone has no unique fundamental, so only the sieve's own
  tolerance can be asserted. The 220 Hz + 1450 Hz case now matches an independent NumPy
  implementation of the metric to four decimals.
* **The "aliasing estimate" is an off-grid energy ratio**, because that is what is measurable
  in a rendered file: energy that is not within a few bins of a multiple of the sounding
  fundamental. Noise-coloured branches are legitimately off-grid, so it is a
  milestone-to-milestone comparison instrument (the 48/96 kHz goldens carry 8% at density 10 —
  that is M6's number to reduce), not an absolute.
* **Articulation exists before the behaviour it measures.** Attack, hold level, release,
  "sound over" and **self-ended** (the sound stops while the key is down) are measured and
  asserted against synthetic envelopes, but no M2 gate is written in them. M3's acceptance
  matrix ("measured attack/hold/release move in the direction `env_time`/`vol_sus`/`vol_fade`/
  `env_loop`/`env_tilt` imply") is written in those fields; that is why M2 comes first.

The soak also says something M7 owns: 3 scenes in 1000 pass full scale (worst peak 1.0104),
and the dense goldens peak at 0.92–0.97 before master volume is involved. One unconditional
soft clipper and sixteen summing voices is the thing §8.1's `Volume` curve + saturating
limiter replaces.

### M3 — The shared envelope engine (design §6.1.1) — the core of this plan

*Why here: this is the change that moves GenoMosh closest to the reference, and it decides what
"duration" means for every articulation metric the harness now reports.*

- New `SharedEnv` in `Dsp.h` replacing the use of `Env` for amp and mod: one phase clock;
  `T = envTime × keyFollow(env_kf)` with `env_time ≥ 0.9 ⇒ ∞` (R3); repeat iff
  `envLoopPeriod < T`; `env_tilt` as the attack/decay split of *that* shape; curvature exponents
  `exp2(2(v−0.5))` for `vol_atk/vol_dcy/mod_atk/mod_dcy`; `vol_sus = max(0, 2(v−0.5))`;
  `vol_fade` as the second fade after the attack above 0.75; note-off per D-5 (stop looping,
  fixed 180 ms), sample-accurate (the doc allows it; today's first-order-smoothed `Env` is not).
- **The infinite case needs a shape time base (R3).** With `T = ∞`, a naive `phase += dt/T`
  freezes the shape at its start and the note never opens — "infinite duration" would be
  silence. Do it as the reference's wording implies: the **duration** is ∞ (ended only by
  key-up, or by `vol_fade` closing it), while the **shape** runs once over
  `T_shape = envLoopPeriod` when a loop period is set, else a named
  `kInfiniteShapeSeconds` (the duration `env_time = 0.9` maps to), then holds at `vol_sus`.
  Write that in the code comment; it is the only reading in which `env_time > 0.9` is usable.
- Remove `loopEnvOneShot`/`loopMod` and the `modSource` selector; the mod envelope is the
  documented one, scaled by `VelSens × mod_vel`.
- Recalibrate `safetyScore`/`applyCouplings` for the new genes: with duration-as-a-gene and
  `vol_sus` gated at 0.5, **silence becomes far more likely** — the doc calls this check
  load-bearing. **M2 measured the audio consequence, and it is this milestone's to fix**: eleven
  of the sixty batch cases are 17-24 dB quieter than their seed, all of them shrill
  non-sustaining blips (`Implementation-Status.md` §12). The batch gate (`over 6 dB ≤ 11 of
  60`) is set there for M3 to move. Add the new failure modes (period ≥ duration; cycle shorter than the shape;
  `vol_fade` closing the note before it sounds). Today the test gate is "majority of far
  branches pass the safety score" (75%) and the last reported measurement was 720/720; raise the
  gate to a hard 100% of *resolved* branches, and let the harness report the ratio.
- New tests + harness assertions: the articulation matrix (does measured attack/hold/release move
  in the direction `env_time`/`vol_sus`/`vol_fade`/`env_loop`/`env_tilt` imply), one-shot when
  period ≥ duration, ∞ above 0.9 (held note keeps sounding past `kInfiniteShapeSeconds`, and
  `vol_fade` alone can end it), key-follow shortening, fixed-release-after-note-off timing.

**Gate:** articulation metrics track the genes across the 12×5 batch; `vol_sus < 0.5` measures no
sustain; 100% of resolved branches pass the recalibrated safety score; bit-identical
reruns at 3 sample rates. Plus the audio half of the §6.3 list: the eleven branches M2's
batch measured beyond ±6 dB of their seed are inside the target by the end of this milestone.
**Status: landed 2026-10-30.** `Dsp.h` has no `Env` any more: `EnvShape` + `SharedEnv` are the
clock, `VoiceParams` resolves it (duration, key follow, loop period, whether it repeats, the
shape's finite base, the four curvatures as exponents, the gated sustain, the fade depth, the
note-off release) and `Voice` only runs it. `loopEnvOneShot`, `loopMod`, the `modSource`
selector and the four `provisionalSeconds` time genes are gone; the mod envelope is the same
clock through `mod_atk`/`mod_dcy`, scaled once by `VelSens × mod_vel` instead of at every
destination. `safetyScore`, `applyCouplings` and `loudnessCompensation` read *effective*
values (`effectiveSustain`, `effectiveDurationSeconds`, `envelopeDuty` in `Mutation.h`), which
is the §6.3 item this milestone owned.

**The eleven were not the envelope.** Named rather than counted (the batch now prints a `!!`
line for every case outside the target), every offender had one cause and it was a trait:
`filterMode`, a structural trait that *replaced* the filter topology outright, against R10, had
grown a branch into "band" or "high" and left a 5-20 kHz pass band where its fundamental used
to be. That is the whole three-metric signature of §12: centroid 2-22 kHz, hold level near
silence, 17-25 dB down. The same reasoning had already scheduled that trait's removal for M5;
M3's measurement is what made it a one line deletion now. With it gone, and with the
duration-aware duty term in `loudnessCompensation`, the batch is **60 of 60 inside ±6 dB**,
worst **+3.64 dB**, quietest **-1.58 dB**, worst deviation by branch length 0.00 / 0.19 / 0.43
/ 1.42 / 3.64 dB, centroid range 238-1219 Hz, safety score **720 / 720**.

**Two things to know before M4.** (1) The instrument at rest is about **8.4 dB quieter** than
§12's table (rms 0.0274 against 0.072): a note now spends half of `env_time` attacking and
lands on a sustain floor instead of sitting at full level. That is the design, and **M7 tunes
the output against §13's numbers**, exactly as it was told to. The post-M3 baseline is pinned in
`testM2BaselineAudio`. (2) A repeat is a whole envelope at the loop rate. The §6.1.1 sketch let
a short period *truncate* the shape, which makes `env_time` and `env_loop` fight over one
envelope and leaves a fast period near silence; the deviation is argued in the comment above the
envelope block in `VoiceParams.cpp`. Related: past the fallback base `env_loop` stops repeating
at all, which is the only reading in which "`vol_fade` alone ends a note that has no duration"
is a real behaviour — and it is tested as one.

The gate is now the target itself: `overSixDb == 0` for the batch, every resolved branch
passing the safety score, `testSharedEnvelope` (the clock: repeats, one shots, the R3 infinite
case, the fixed release) and `testEnvelopeArticulation` (measured attack, hold, sound-over,
self ending and key follow move as the genes say). Full record:
`Implementation-Status.md` §13.

**Unlocks:** M4 (the mod envelope now *is* the note envelope — every modulator's shape depends on
it) and the articulation half of M10.

### M4 — Modulation sources and destinations

*Why here: with one clock, `a_mod`/`b_mod` and the LFO become the two things that make a patch
move, and their shapes are now well-defined.*

- `a_mod`/`b_mod` → **bipolar pitch modulation** from the mod envelope, verbatim as the reference
  describes it (R2): `0.5` = no pitch modulation, `0.0` = maximum negative, `1.0` = maximum
  positive, **no dead zone** — identically on A and on B. Depth stays bounded by our ceiling
  (±12 st at atonality 1), scaled by `VelSens × mod_vel`, tagged `kFlagPitch`.
- LFO: → **pitch + tremolo** at one rate; equal-power split by `lfo_bal` (0 vibrato / .5 both /
  1 tremolo); `lfo_amt` bipolar with the documented polarity (`< 0.5` = A and B in opposite
  phase); `lfo_dly` onset fade above 0.5; remove the fixed LFO→filter path (R6); stop randomising
  the LFO shape per voice (keep a phase offset).
- Two real **S&H** sources: `mod_sh` re-quantises the mod envelope output (off below 0.75, 8 Hz →
  0.1 Hz inverted) and `b_sh` is a rate on B, note-synced (`B ÷ 1…8`) below 0.5. These replace
  the current `mod_sh` curve exponent and the `b_sh` mode gate. Keep ring-mod/sync as *structural
  trait* possibilities (R10), not as `b_sh` semantics.
- `mix_mod`: mod envelope → A/B mix, bipolar, feeding the same equal-power curve as `osc_mix`.
- Smoothing per §6.4: S&H steps must not be ramped away. Keep drift + per-voice detune/phase as
  non-gene life (D-3) with fixed amounts.

**Gate:** one rate produces vibrato and tremolo at once at `lfo_bal = 0.5` (measured from a
render: pitch modulation and amplitude modulation at the same frequency); `lfo_amt < 0.5`
decorrelates A and B in opposite phase; S&H steps visible in the waveform (edge-slope metric);
`lfo_dly` measures a silent onset of the documented length; pitch-mod depth never exceeds the
ceiling and is **zero at atonality 0** (this replaces `testPitchStability`'s current assertion —
see §6.1).
**Unlocks:** M5 (filter cutoff now has a defined modulator budget) and M8's `converge()` coupling.

### M5 — The filter network as documented

*Why here: it needs M1's stepped mapping and M4's modulation budget, and it changes the loudness
model that M7's output stage must compensate for.*

- `flt_type` **as a gene** (Rev 2 D8), four topologies: 0.0 bandpass · 0.33 dual **serial** LP ·
  0.66 dual **parallel** LP (default) · 1.0 notch. Remove the `filterMode` trait (R10: a trait may
  offset the gene by a step, never replace it).
- Topology changes are **crossfaded**, never slewed: keep two or three rendered branches and
  crossfade between them when the stepped value changes (this also finally makes §5.4's
  crossfade requirement observable/testable).
- `flt_sep` → bipolar around 0.5 = no separation, ±3 oct; `flt_kf` → `softRails(v, 0.25, 0.75)` ×
  1 octave/key; `flt_freq` → `fA × 2^lerp(−2,+8,v)` where `fA` comes from the resolved
  `a_freq` + note (M1's link), default 0.75; per-topology Q and separation caps in
  `applyCouplings` (no self-oscillation, and a notch/BP is far quieter than an LP —
  `loudnessCompensation` must learn the topology).
- Zero-delay-feedback sections stay (already trapezoidal); add the parallel sum and the notch mix.

**Gate:** cutoff tracks the keyboard (measured spectral centroid per octave stays within a stated
tolerance — this is design §14 phase 3's `flt_freq` gate); topology crossfades produce no click
(max first-difference metric); `flt_type` at each of the four steps is audible and level-safe
(branch loudness still within ±6 dB); `flt_sep` at 0.5 measures zero separation.
**Unlocks:** M6 (saturation placement relative to the filter is now unambiguous) and M7 (the
output stage must compensate a filter it can finally model).

### M6 — Voice quality: the things that make it not sound cheap

*Why here: the DSP fixes are cheap once the graph is settled, and the aliasing metric from M2
makes them measurable instead of plausible.*

- `saturate` as a real **per-voice stage after the filter**: 2× oversampled, DC-blocked, off
  until ~0.5 then rising drive (design §7; the `Rev2-Answers` A2 conflict is design Q-5 —
  implement the reference's description, mark the code with the question).
- True **sub-AM**: B ÷ 2 ring-biased at B ÷ 2, mixed pre-`osc_mix`, replacing the sine sub.
- **Noise colour**: one shared brown→white shaper driven by `a_color` (design Q-7: one shared
  source), feeding `a_noise`/`b_noise`; the `b_color` **extra** (R1) is a per-B colour *offset*
  from that shaper, silent at 0.5.
- Waveform morph at the documented breakpoints (sine 0 / saw 0.57 / square 0.81 / pulse 1.0),
  band-limited rather than crossfaded shapes; PWM assumed fixed (design Q-4) — no new gene.
- **Oversample the FM/PM path** 2–4×, or band-limit it; aliasing is what the doc says makes
  digital synths sound cheap.
- Crossfade structural traits inside the voice where the graph allows; **fade stolen voices**
  (D16) instead of restarting them.

**Gate:** aliasing estimate drops measurably for FM at `fm_amt ≥ 0.5` on high notes; sub tracks
`b_freq` (measure a −12 st relation); `a_color` sweeps brown→white in the rendered spectrum;
stolen voices show a fade, not a discontinuity; no new loudness outliers (±6 dB still holds).
**Unlocks:** M7 — the voice's output level is finally predictable, so the master chain can be
tuned against it.

### M7 — Output stage, macros with their documented curves, reverb behaviour

*Why here: `adj_*` and the macro curves sit at the end of the chain and touch the guard rails'
level model; putting them earlier would have made M3–M6 impossible to measure in dB.*

- `adj_bass`/`adj_treb` shelves (±24 dB at 100 Hz / 6 kHz), `adj_pan` (branch-position spread ×
  `Effect`'s pan half), `adj_clip` as a **driven master soft clipper after master volume**.
  R5 settles the master-chain order: per-voice dry + wet → sum → shelves → `Volume` →
  limiter/`adj_clip` (design Q-8 answered). All four `kFlagNever`.
- `Volume` = `(2v)^log2(10)` into a **saturating limiter** whose drive is the same value (§8.1),
  replacing `raw × 1.15 + softClip`.
- `Tuning` → ±12 semitones; `Release` macro = `50 ms × 400^v` **plus the per-voice wet gate**
  (§8; still separate from the amp's fixed 180 ms release); `Effect` gains its pan half;
  per-branch `volume` trim and `enabled` flags in patch state (D-2).
- **Reverb moves into the voice (R5).** Each active voice owns a `rvb_*` instance: its own FDN
  tail, its own `rvb_atk` wet-amount bloom above 0.66, its own gate from the `Release` macro,
  its own `fx_width`. `MasterFx` keeps only the output stage. Chorus taps stay inside/across that
  voice's tail per §8; `rvb_size` still changes only from a **direct edit**, never from growth.
- **The cost is what that answer buys.** Because the wet signals sum, per-voice fidelity is not
  needed: 4-line Hadamard instead of 8, one input allpass instead of two, run the tail at half
  rate (damping already throws HF away), 2 chorus taps instead of 4, share coefficient maths
  between voices, and scale tail quality down as the active-voice count rises. Keep the whole
  thing behind one `WetPath` interface so the shared-bus variant stays a compile-time A/B.
- Recheck `loudnessCompensation` with shelves + topology + clipper **and N-voice wet summation**
  modelled — 16 independent tails summing is a level change, not a rounding error.

**Gate:** the `Volume` curve measured (0 dB at 0.5, +20 dB at 1.0, and distortion onset behaves);
`Release` low values audibly gate the tail (tail length measured with the gate on/off);
`rvb_atk` bloom measured as wet-rise time **per voice**; branch pan spread scales with `adj_pan`
and falls as `Effect` falls; the five `kFlagNever` genes never move under growth;
**per-voice reverb CPU measured at 1 / 4 / 8 / 16 active voices against a stated budget** — this
is the number R5's answer depends on, and if it does not fit, the A/B behind `WetPath` is the
fallback, not a silent quality cut.
**Unlocks:** M8 — growth can now be judged against a chain that behaves consistently.

### M8 — Growth and curation semantics (§5.3, §5.5, §5.7, §5.8)

*Why here: it is last of the audio milestones because it changes how the previous ones are
navigated, and it is nearly all pure gene-space code with cheap gates.*

- Wheel: `lerp(branchLength, 1.0, clamp01(wheel × wheelScale))`; wheel parameter default **0**;
  CC1 and the host parameter reconciled (no silent override); minimal `Wheel Target`
  (growth / LFO amount / cutoff — design Q-3).
- Rotation stays **out** (R4): `branchForNote = (note − rootNote) mod 12`, no macro, no offset,
  no handle.
- Atonality = `converge(x, at)` on `a_mod`/`b_mod` at generation time (R7), plus the D-4 ratio
  snap on `b_freq` blended by the same value; `a_freq` stays quantised to the 7-step grid;
  delete the 5-gene hard ceiling and `pitchNeutral`'s unipolar special case.
- Planting per §5.8: set all twelve lengths to that branch's length and `seedId += branchIndex`;
  stop flattening the grown genome into the seed and resetting lengths to 0.45.
- The §10 **per-branch resolve cache** (keyed by snapshot revision + quantised distance) so
  `startVoice` stops re-running up to three full-genome guard-rail passes per note.
- Structural traits retargeted per R10 (offset stepped genes, swap which modulator drives a
  destination, move `b_sh`/`mod_sh` in and out of sync mode) using the reference's own
  breakpoints as thresholds (§5.4).
- Seed codes (§5.7.9): base-32 over `23456789abcdefghjkmnopqrstuvwxyz`, plus word-list names
  (a small shipped table) — display and parse both ways.

**Gate:** length 0 returns the seed exactly (unchanged); deviation monotone **in gene space**
(stepped genes make audio-space monotonicity false — assert in gene space, see §6.4); deviation
zero for `kFlagNever` genes; atonality 0 ⇒ `a_mod = b_mod = 0.5` and `b_freq` on the ratio
table, atonality 1 ⇒ untouched, and both are idempotent under repeated planting; the resolve
cache is bit-identical to uncached resolution; a typed seed code round-trips.
**Unlocks:** M9 (host parameters and MIDI now have stable semantics to expose) and M10.

### M9 — Host and state integration (§9, §11)

- 48 gene parameters in one `Genome` unit/group (R9: they write the authoritative seed), with
  raw `0..1` display, the 48 in document order first and the extras block after them (R1), plus
  the macro parameters from M7 and the missing ones (voice mode + glide, tempo sync,
  `PitchWheelRange`, MIDI channels, velocity curves). No `Rotation` parameter (R4).
- VST3 `IMidiMapping` + `IMidiLearn`; CC-bound **actions** (undo/redo/prev/next/plant) rather
  than `TriggerA–H`; program changes and a small factory bank; automatable branch lengths
  (already true) + per-branch trims.
- Pitch bend, sustain pedal, mono/legato + glide, tempo sync that degrades gracefully when the
  host gives no tempo.
- State: `formatVersion` / `modelVersion` / `mutationVersion` as three separate axes (design
  §11), branch records as `{id = seedId + i, length, volume, enabled}`, `seedId`, `pitchAdjust`,
  `name`.

**Gate:** a saved instance recalls bit-identically (parameters + seed + branch records);
automating a gene parameter changes the plant the same way the UI does (one source of truth);
bend/pedal/mono tested in `GenoMosh_Tests` at the `Synth` level; VST3 loads in a DAW with the
gene group visible and MIDI-learn working.
**Unlocks:** M10 — the tuning loop needs the whole surface reachable from a DAW.

### M10 — Tuning the space (design §16 priority 1)

Iterate `sensitivity`, `softMin/softMax`, `gateLow/High`, `neutral`, `steps`, couplings and
`helpText` against the harness plus listening sessions: random seeds × 12 branches × 5 lengths,
report the **usable-branch ratio** and the loudness spread, and log which genes cause failures.
This is where §4.2's `Sens` column stops being a guess. Keep every change behind the golden
protocol so a retune cannot silently break a previously-clean case.

**Gate:** a stated, reproducible usable-branch percentage over a fixed seed list (target: "a
random seed plus any branch is usable most of the time", §14 phase 7), plus a written tuning log.

### M11 — UI: per-gene pages, plant polish (§12)

Three gene pages matching the document's grouping, with group colours, hover `helpText` from the
table, raw `0..1` readouts, **grown-vs-base value display**, and per-group response plots
(envelope, LFO, filter response, reverb), plus a separate "GenoMosh additions" section for the
extras block (R1). Plant view: mod-wheel arc, per-branch trim/enable affordances, hover note
names — no rotation handle (R4). The UI reads resolved values through the same mapping layer as
the audio engine — never its own copy of a curve.

**Gate:** the plant view never blocks the audio thread (measure with the plugin under load);
every gene is reachable and legible; a 48-gene page is navigable using only colour + help text.

---

## 6. Details that will bite (new in Rev 3)

### 6.1 `a_mod`/`b_mod` go back to pitch, and that has a history
On 2026-10-21 the code **removed** mod-envelope→pitch as a bug fix ("the note changes pitch when
I play it", `Implementation-Status` §8), and `testPitchStability` asserts that removal today.
Rev 3 says those genes *are* bipolar pitch modulation from the mod envelope, and that Atonality's
documented job is to bias exactly those two genes toward neutral. So the reference does what we
deleted — with two differences we must copy: bounded depth, and generation-time convergence
toward neutral so playable plants have little of it. Implement R2 with an explicit depth ceiling
and **rewrite** the gate to: *zero pitch deviation at atonality 0, ≤ ceiling at atonality 1, no
pitch change from the shared envelope when both genes are neutral*. Do not keep the old assertion
and do not keep the old behaviour — that would silently cancel M4. Note the interaction with M3:
with duration-as-a-gene, a short cycle makes this a dive and a long cycle makes a swell. That is
the reference's behaviour; it must be measured, not defended.

Answered on 2026-10-29 (R2), with the reference's own text quoted in §4: `a_mod` is *"bipolar and
determines the pitch modulation amount from the Modulation Envelope. 0.5 means no pitch
modulation. 0.0 is the maximum negative, and 1.0 is the maximum positive modulation"*, and the
same holds for `b_mod`. Two details that follow, both cheap to get wrong: the mapping is
centred-silent (no dead zone, unlike `lfo_amt`'s documented v1 behaviour), and the endpoints are
**full** swing, so the ceiling and `converge()` are the only things keeping a playable plant in
tune. `testPitchStability` must be rewritten in the same commit that lands the behaviour.

### 6.2 Envelope times are fractions of a duration that can be infinite (R3)
`vol_atk`/`vol_dcy` stop being seconds. A cycle shorter than the old attack+decay is no longer
"legal but odd" — it is the normal case, and the shape must fit inside `T`. R3 confirms the
answer (fractions of `T`) and adds the reference's own ceiling: *"env_time controls the total
duration of the sound. Above 0.9, the Envelope will have infinite duration, stopped only by
releasing the key or vol_fade fades out."* Two things to write into the code comment once, so
the guard rails stop fighting the engine:

- shape segment times are fractions of `T`, never absolute;
- `env_time ≥ 0.9` means **duration** ∞, not **shape** ∞ — the shape needs the fallback time
  base described in M3, or `phase/T → 0` and the note never opens. The articulation metric in
  M2 must treat "held until key-up" and "ended by `vol_fade`" as valid measured outcomes.

### 6.3 Guard rails learn new genes late
`safetyScore` and `loudnessCompensation` today read `vol_atk/vol_dcy/vol_sus/flt_freq/sat_drive/
sub_am/fm_amt` with Rev 1 meanings. After M3, M5 and M7 they must read: duration, gated sustain,
`vol_fade`, loop period, **filter topology**, **shelves**, **clipper**, tracking cutoff. The
reported 720/720 usable-branch rate and the ~1 dB level match are the regression to watch; both
are hard gates at every milestone that changes them. **Measured at M2, the real position is**:
49 of 60 grown cases within ±6 dB of their seed, eleven outliers (all shrill, non-sustaining),
and no level problem whatsoever below branch length 0.25. The 720/720 figure is a
safety-score claim, not an audio one — the audio version of it is the batch table in
`Implementation-Status.md` §12.

### 6.4 Stepped genes break "deviation grows with distance" *in audio space*
`a_freq` (7) and `flt_type` (4) move in jumps, so a monotone audio-distance assertion becomes
false. Assert monotonicity in **gene space**, and separately assert that crossing a step is
crossfaded rather than clicked.

### 6.5 Per-voice reverb: what R5 costs
R5 was answered *for* per-voice wet paths and against this plan's default (a `Synth`-level
wet-send bloom), with the explicit qualifier "heavily optimized — quality per-voice isn't needed
when the wet signals are combined". That is the honest reading: `rvb_atk` and the `Release` gate
want to be per-voice behaviours (a shared bus makes them key-count dependent, which is exactly the
code smell described in Rev 2 plan §5.3), and summing many mediocre decorrelated tails is a
legitimate way to buy a big one. What it costs, and where the gate is:

- 16 tails instead of one. The optimizations listed in M7 (half the lines, half the rate, fewer
  taps, shared coefficients) are what make that affordable — do them **with** the move, not after.
- Voice lifetime becomes tail lifetime. Stolen/faded voices (D16) must keep their tail alive
  until it is silent, or a steal cuts a reverb off; the voice pool needs a "wet tail still
  ringing" state. This is the part most likely to bite.
- Levels: 16 independent wet paths summing is a real change to `loudnessCompensation`.
- `MasterFx` shrinks to the output stage only, and `fx_width` (kept as an extra, R1) now
  decorrelates each voice's wet signal instead of the whole bus — which is better, but must not
  be applied twice.

Keep both implementations behind one `WetPath` interface. If the CPU gate at M7 fails, the
shared-bus variant is a deliberate, measured fallback — not a silent quality reduction.

### 6.6 `adj_pan` smears chords, and it fights today's per-voice random pan
Design's pan is a function of **branch** (i.e. of pitch class), which is stable across octaves.
Replacing the random per-voice spread with it is intended (D-2/D-3), but a chord then spans the
field by construction. R12's variance bound is the mitigation if it sounds wrong.

### 6.7 Two control planes for the same 48 values — settled
§9's gene parameters plus the plant's own seed genome was a genuine split-brain risk. R9 decides
it: the parameters **are** the store (`AudioParameterFloat`s in a `Genome` unit, listener writes
the seed genome), and XML stays serialisation only. Practical consequence for M0/M9: nothing may
copy gene values into a second live structure; the UI and the host both read and write the same
APVTS values, and the seed genome is derived from them.

### 6.8 Housekeeping — done (2026-10-30, with M0)
The three gene-page screenshots in `Pictures/2026-10-22-0854xx/0855xx_hyprshot.png` had no
`.txt` sidecars (the project convention, and design §15 Q-2 asks about them). They have been
written, page by page: which gene ids sit in which group, what each figure plots, and what the
asterisks on `lfo_amt`/`adj_clip` do and do not tell us. Design §15 Q-2 and the provenance note
are updated to match.

---

## 7. Order, dependencies, and what is deliberately not in this plan

Recommended sequence: **M0 → M1 → M2 → M3 → M4 → M5 → M6 → M7 → M8 → M9 → M10 → M11**,
with M2 allowed to start as soon as M0 compiles, and M11's static parts (colours, help text)
possible any time after M1.

Why this order rather than the design document's §14 numbering (mapping: M0 = §14 phase 0,
M1 = phase 1, M3+M4 = phase 4, M5+M6 = phase 3, M7 = phase 5, M8 = phase 2 + §5.7, M9 = phase 6,
M10 = phase 7, M11 = phase 8): §14 lists the *voice* before *modulation*, and *growth* early.
Both orderings are defensible, but with the current code the voice and filter already exist and
work, so re-semantifying them (M5/M6) is cheaper after the envelope and modulation sources are
correct (M3/M4), and growth semantics (M8) can only be finalised once the genes they scale have
their final meaning. Tooling is pulled ahead of the sound changes for the reason Rev 2's plan
gave: nothing about the gene ranges has ever been measured.

Not in this plan, deliberately: **`Rotation` in any form (R4 — removed, and the design document
now says so)**, `layers` / `Bulb Mode` / `Voice Mode`'s full semantics, the complete 9-value
`Wheel Target` and 7-value `Tempo Sync` sets, `a_form` PWM, breeding/crossover, audio-to-patch
matching, MPE, the plant-view rewrite beyond M11's listed items, held-note re-resolution (D9, now
a debug-mode affordance), and CLAP/standalone targets (design Q-10).

---

## 8. Definition of done for this plan

- `GENO_MOSH_GENE_LIST` is exactly the 48 genes of design §4.2 by id, order, group, curve,
  `steps`, `neutral`, gates and `kFlagNever`, proven by `testTableMatchesDoc`, and every gene
  carries `helpText`. After them sits a separate, marked extras block of exactly two genes
  (`b_color`, `fx_width`), recorded as design deviation D-7 — nothing else (R1).
- There is **no ADSR path** in `Voice`: amp and modulation envelopes come from one shared clock
  with `env_time`/`env_loop`/`env_tilt`/`env_kf` and curvature genes, with the D-5 note-off.
- Modulation destinations are as documented: `a_mod`/`b_mod` on pitch (bounded), LFO on pitch +
  volume with `lfo_bal`/`lfo_dly`, two S&H sources, `mix_mod`, `fm_mod`, `flt_mod`.
- `flt_type` is a stepped **gene** with all four topologies crossfaded; the filter tracks
  `a_freq`; per-voice `saturate` and the `adj_*` output stage exist, and all five
  growth-exempt genes provably never move.
- The reverb lives **in the voice** with its own `rvb_atk` bloom and its own `Release` gate, the
  optimizations in M7 are in place, and the per-voice CPU cost is measured at 1/4/8/16 voices
  against a stated budget (R5).
- `env_time ≥ 0.9` behaves as the reference describes: sound held until key-up or until
  `vol_fade` closes it, with a defined shape time base (R3).
- There is **no `Rotation`** anywhere: no parameter, no note-map offset, no UI handle (R4).
- A 43-gene saved state is **refused with a warning**, not half-migrated (R11).
- The nine §5.7 mechanisms are implemented as *data* and each has a unit test naming it.
- The §13 protocol exists as described: `createTest`/`performTest` with `tolerance`, five sample
  rates, the 12×5 batch, `massiveAudioTest`, WAV output, level/spectral/aliasing/articulation
  metrics, and a recorded M10 tuning report with a usable-branch ratio.
- `ctest` is green at 44.1/48/96 kHz, reruns are bit-identical, loudness stays within ±6 dB, and
  the pitch, atonality and note-bookkeeping tests exist in **re-based** form (they assert the
  documented semantics, not Rev 1's).
- `Docs/Implementation-Status.md` carries a dated Rev-3 alignment section; `Docs/Rev2-Alignment-
  Plan.md` is marked superseded; the deviations the design doc lists (D-1, D-3, D-5, D-6) plus the
  new ones from these answers (D-7, the extras block) are recorded in §3.2 of the design document.

---

## 9. Decision record (2026-10-29)

Every question this plan asked was answered on 2026-10-29; the decisions live in §4 and the
verbatim answers in `Docs/Rev3-ChangeLog.md` §5. Kept here so the history is legible — the ones
that had been marked **(B)**, and what each one changed:

| Was asked | Answer | What it moved |
| --- | --- | --- |
| R1 exactly 48, or 48 + extras? | 48 **+ documented extras** | §2.1 and M0: 50 slots, `b_color`/`fx_width` demoted to a marked extras block, design deviation **D-7** added; `vol_rel`/`det_spread`/`drift_amt` still deleted |
| R2 `a_mod`/`b_mod` really pitch mod? | Yes, with the reference's bipolar wording quoted | M4 and §6.1: centred-silent at 0.5, full swing at both ends, ceiling + `converge()` kept, `testPitchStability` rewritten |
| R3 fractions of the cycle? | Yes — and `env_time > 0.9` = infinite duration | M3 and §6.2: shape times are fractions of `T`; the ∞ case needs a shape time base |
| R4 restore `Rotation`? | **No** | Design doc corrected (§4.1/§5.3/§9/§12/§14); M8/M9/M11 and §7 now exclude it; D12 closed as originally ruled |
| R5 `rvb_atk`: shared bus or per-voice? | **Per-voice wet paths**, heavily optimized | M7 + §6.5 rewritten: reverb moves into `Voice`, `WetPath` interface, CPU budget becomes a gate; design Q-8/Q-9 closed |
| R11 old states? | Refuse to load, warn, fresh plant, `modelVersion`, no fake migration | M0/M9; design §11's "write a converter" line amended |
| R6 / R7 / R8 / R9 / R10 / R12 | As proposed | Confirmed; design Q-6 (Atonality's reach) closed by R7 |
| D5 / D9 (answered as "D6") / D13 / D16 | Unchanged / no held-note re-resolution / keep / keep with a real fade | Nothing new; D9 is now an M6 debug affordance |

**Still open — deliberately, and none of it blocks M0–M2:**

1. Design **Q-5**, saturation placement: the reference says per-voice `saturate` + master
   `adj_clip`; `Docs/Rev2-Answers.md` A2 says the reverse. Implement the reference's version
   (M6) and mark the code. One graph edge either way.
2. Design Q-1 (layers / Bulb Mode), Q-3 (`Wheel Target`'s 9 values, `Tempo Sync`'s 7), Q-4
   (`a_form` PWM), Q-10 (target formats) — out of scope or defaulted harmlessly.
3. ~~§6.8 housekeeping: the three gene-page screenshots still need `.txt` sidecars (fix in M0).~~
   **Done with M0, 2026-10-30.**
4. Whether the fixed per-voice drift/detune amounts should become a macro. Default: no, they stay
   named constants (D-3). Revisit at M10 with measurements, not impressions.

If anything later looks inconsistent, look first at the three answers that changed the plan's
shape rather than its details: **R1** (a 50-slot table and a marked extras block), **R4** (the
design document had to be edited, not just the plan) and **R5** (the reverb moved into the voice
and brought a CPU budget with it).
