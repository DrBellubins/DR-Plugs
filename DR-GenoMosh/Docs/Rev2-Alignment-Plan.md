# GenoMosh — Rev 2 Alignment Plan

**Status: superseded (2026-10-29).** The design document moved to **Revision 3**, which closed
or overturned most of Rev 2's `[inferred]` gene semantics, so this file's stage list is no
longer the plan of record — use **`Docs/Rev3-Alignment-Plan.md`**. What is still useful here is
§2 (the decision register) and §5 ("details that will bite"); Rev 3's plan re-lists those with
the same `Dn` numbers and says which ones Rev 3 closes, reopens or contradicts. Note that four
rows of §2 were **re-decided on 2026-10-29** when the Rev 3 register was answered: D1/D2 (48
genes **plus** a documented extras block — as ruled here), D12 (Rotation: stays removed, as ruled
here), D15 (replaced by R11: refuse to load, no migration), and D9 (no held-note re-resolution,
an M6 debug affordance only). The current answers live in `Rev3-Alignment-Plan.md` §4.

*Plan of record for bringing the implementation in line with `Docs/Genetic Synth - Design
Document for Local LLM.md` **Revision 2** (commit `aff2dae`, "Design document corrections").
Rev 2 revised §4.2 (the exact 48-gene list), §6 (the shared looping envelope engine, LFO
destinations), §7 (voice architecture, `adj_*` placement) and §8 (reverb + output stage).
The code at `1f245e9` was written against Rev 1 and has not been reconciled.*

This document is a plan, not a status report. `Docs/Implementation-Status.md` stays the
record of *what is built and measured*; this file is the record of *what is going to change,
in what order, with which gate*, plus everything still undecided.

**Convention:** every `§n` in this file means a section of the **design document**. This
plan's own sections are referred to as "this plan §n", and work items by Stage letter.

Companion reading: `Docs/Morphing synth.md` (the project's own direction — more movement
while playing than the reference product). Rev 2's §6.1.1 is exactly in that direction, so
where Rev 1 and Rev 2 disagree, **Rev 2 wins**.

---

## 0. Why this plan exists

Rev 2 changed four things that the code cannot absorb with small patches:

1. **The gene list is now exact** — 16 + 15 + 17 = 48. Rev 1's "about 38 genes, and here
   are candidates to reach ~48" is gone, and Rev 2 says the candidate list (detune/spread,
   per-voice drift, release time, stereo width, noise colour, glide) is *dropped*, and that
   per-voice variation and slow drift stay as **non-gene voice behaviour** (§7).
   The current genome is **43 genes**: 38 of the doc's 48, plus 5 of those dropped
   candidates, minus 10 the doc now names.
2. **There is no conventional ADSR in the design any more.** §6.1.1 replaces
   "ADSR plus a third modulator" with one **shared cycle clock** that produces both the amp
   and the modulation envelope, plus `env_kf`, `vol_fade`, and a defined note-off
   behaviour ("stop looping, short fixed release").
3. **The LFO's destinations are pitch and volume**, and the LFO→filter destination that the
   code uses today is explicitly dropped. `lfo_dly` is new.
4. **The output stage (`adj_bass`, `adj_treb`, `adj_pan`, `adj_clip`) and `rvb_atk`** are now
   specified genes (§8.1), and `flt_type` / `mix_mod` / `sub_am` / `saturate` are named.

Everything else in Rev 2 (the mutation engine §5, the real-time rules §10, the FDN-with-
chorus shape §8, the plant view §12) is already implemented in shape, and in §10's case
completely.

**Priority of work follows the document's own §16:** curation of the gene space and the
voice first, UI and extras last. The stages below are ordered so that each one makes the
*next* one measurable, and the offline measurement tooling (Stage I) lands early enough to
inform the tuning rather than to audit it at the end. That is a deliberate deviation from
the document's phase numbering, where the harness is only implied by §13.

---

## 1. Ground rules that constrain every stage

Restated from the design document §0/§10 and from what this codebase already enforces,
because every stage below has to keep them true:

- `Source/Genetic/` and `Source/Engine/` stay **free of JUCE types** (the offline harness
  drives them directly). This is load-bearing, not stylistic.
- **No allocation, locks, or I/O on the audio thread.** Anything new must be preallocated in
  `prepareToPlay` / `Voice::prepare`.
- **Deterministic PRNG only** (splitmix64 + FNV in `Genetic/Rng.h`). Never
  `std::normal_distribution`.
- The repo builds with JUCE's recommended warnings: `-Wsign-conversion`, `-Wshadow`,
  `-Wfloat-equal`, `-Wswitch-enum`. Index `std::array` with `std::size_t`; do not shadow
  JUCE base-class members.
- `GENO_MOSH_GENE_LIST` in `Genetic/GeneTable.h` is the **single source of truth** for both
  the `GeneId` enum and `kGenes[]`. New genes are added there, nowhere else.
- Pitch policy is settled and must not regress (Implementation-Status §8/§9, doc §5.3):
  the played pitch is the note times the `a_freq`/`b_freq` offset, plus LFO vibrato and a few
  cents of drift. **The modulation envelope must never reach pitch.** The gate that
  guarantees this is `testPitchStability` (autocorrelation of two windows of one held note,
  must agree within 15 cents and land on the pressed note).
- Every stage ends with `ctest` green, not with a claim. Build/run recipe:

  ```sh
  cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build/dev
  ctest --test-dir build/dev
  ```

- **Do not implement** (Rev 2 is explicit): `TriggerA…TriggerH` (§4.1, `[unknown]`), any
  special behaviour for the asterisked `lfo_amt` / `adj_clip` (§4.2, `[unknown]`), audio-to-
  patch matching, ML, MPE, cloud (§0).

---

## 2. Decision register (undecided items)

These are the questions the alignment actually turns on. Each has the **default this plan
proceeds with**, marked *(defaults)* so work is never blocked, and each default is written
into code with a comment naming this table. Anything marked **needs the operator** changes
the sound in a way that should not be chosen silently.

| # | Question | Options | Default | Why |
| --- | --- | --- | --- | --- |
| D1 | What happens to the 5 genes Rev 2 dropped: `vol_rel`, `b_color`, `det_spread`, `fx_width`, `drift_amt`? | (a) delete; (b) keep as data but `kFlagNever`; (c) keep mutating | **(c) for `b_color`/`fx_width`, (a→moved to constants) for `vol_rel`/`det_spread`/`drift_amt`** | See this plan §3.2. `b_color` and `fx_width` are cheap and widen the space; the other three are either replaced by a Rev 2 gene (`vol_rel`→`vol_fade`) or explicitly demoted by Rev 2 §7 (drift/detune are voice behaviour, not genes). |
| D2 | Is the genome **exactly 48**, or 48 + a small number of documented extras? | strict / strict+extras | **strict 48 in the table's page order, extras appended in a separate clearly-marked block** | Preserves the document's vocabulary contract while keeping what the extra genes buy us; the UI can show the 48 as three pages and the extras under "GenoMosh additions". |
| D3 | `env_loop` semantics | (a) depth of re-excitation (Rev 2's suggested start); (b) repeat count; (c) loop region | **(a)** | §6.1.1 suggests it and says keep it swappable. Implemented behind one function (`LoopEnv::retrigger`) so (b)/(c) are a small change. **needs the operator** eventually. |
| D4 | `vol_fade` semantics | (a) per-repeat peak fade (Rev 2's reading); (b) release stage | **(a)**, with `vol_rel` kept as an internal constant release, not a gene | Figure 1a shows stepping peaks; a real release stage still has to exist, so it comes from `rvb`-independent fixed timing until the operator decides. **needs the operator.** |
| D5 | Note-off behaviour (§6.1.1 `[unknown]`) | stop looping + fixed 50–300 ms release / keep looping after note-off / free-run | **stop looping + 180 ms fixed release** | Exactly what Rev 2 says to do until clarified. Flag in code. |
| D6 | Does the LFO keep a filter destination at all? | drop entirely (Rev 2) / keep at low depth / keep as a *structural* reroute | **drop from the fixed routing; add it as one of the §6.3 structural routings** | Rev 2 drops it as a default destination, but filter tremolo is one of the most useful "alive" effects we have and §6.3 point 3 ("rarely add a routing") is where it belongs. Deviation, recorded. |
| D7 | Is `lfo_amt` still a `kFlagPitch` gene now that it also does tremolo? | keep tagged / untag and bound only the pitch component at resolve time | **untag; keep an `lfo_amt` gene that is not pitch-tagged, and bound the *pitch component* of its output inside the voice** | Otherwise atonality 0 silences tremolo too, which is wrong: atonality is about tuning, not about movement. This is a real change to `applyAtonality`'s gene set. |
| D8 | `flt_type` as a stepped **gene** (Rev 2 §4.2) vs. today's structural trait | gene / trait / both | **gene with a new `Curve::Stepped`, trait path removed** | Rev 2 lists it among the 17 page-III genes. Keeping both would double-count. |
| D9 | Do we implement Rev 2 §3's *optional* "crossfade to the new genome over ~50 ms for held notes"? | yes / no | **no, not in this plan** — but note that this is the only thing that would make §5.4's "each structural change must be crossfaded in the voice" observable, since voices capture their genome at note-on. **needs the operator** (it is a sound-design decision about dragging a branch while holding a note). |
| D10 | `b_sh` — sync / ring / S&H mode selector (current) vs. one continuous control | current / continuous | **current (mode chosen by structural mutation, `b_sh` = amount)** | Rev 2 marks it `[inferred, uncertain]` and says "make it swappable". It already is. |
| D11 | Atonality: keep the generation-time ceiling, or Rev 2 §5.5's play-time blend? | ceiling (current) / blend (doc text) | **ceiling**, and write an addendum into the design doc | The ceiling is idempotent, which is what makes planting a branch safe (Implementation-Status §9). The doc text is stale, not the code. |
| D12 | Rotation stays removed? (Rev 2 §4.1/§5.3/§12 still describe it) | keep removed / bring back | **keep removed**, doc addendum | Operator's verdict, 2026-10-21. |
| D13 | `softCompress` vs Rev 2 §5.2 step 4's `reflect01` | keep softCompress / switch | **keep softCompress**, doc addendum; `reflect01` stays in `Curves.h` for A/B | Monotonicity is a §5.1 requirement that reflection silently breaks. |
| D14 | Does the audio thread keep resolving **directions** (current) instead of Rev 2 §3's 12 pre-resolved genomes? | keep / change | **keep**, doc addendum | It is what lets the mod wheel open a plant live without allocation. |
| D15 | Preset compatibility: what happens to v1 saved plants when the table changes? | break them / migrate | **`mutationVersion = 2` + an explicit v1→v2 gene mapping in `setStateInformation`**; unmapped old genes land at their table neutral, dropped genes are ignored | §11 asks for a versioned mutation. A saved plant should not become noise. **needs the operator** if they prefer "v1 just stops loading". |
| D16 | Polyphony budget (Rev 2 §7: 8–16) | 16 / lower | **16, unchanged**, but with a real fade on stolen voices | Open question 4 in the design doc is still unanswered. |

---

## 3. The gene table: exact target

### 3.1 The 48 (Rev 2 page order)

The table below is what `GENO_MOSH_GENE_LIST` should contain after Stage A. Curves,
sensitivities and ranges are Rev 2's starting values; `softLo/softHi` are ours. Ranges
that differ from the current table are marked.

**Page I — Envelope and LFO (16)**

| Gene | Curve | Range | S | Notes |
| --- | --- | --- | --- | --- |
| `env_time` | Exp | **0.020 – 10.0 s** | 0.5 | *was 0.050–16.0* — Rev 2's range is tighter at both ends |
| `env_loop` | Linear | 0–1 | 0.3 | becomes the loop engine's re-excitation depth (D3) |
| `env_tilt` | Bipolar | −1…1 | 0.4 | warps cycle phase (already the shape of `tiltTriangle`) |
| `env_kf` | Bipolar | −1…1 | 0.3 | **new** — key follow of the cycle *length* |
| `vol_atk` | Exp | 0.001 – **5.0 s** | 0.5 | *was 4.0 s* |
| `vol_dcy` | Exp | 0.010 – 8.0 s | 0.5 | |
| `vol_sus` | Linear | 0–1 | 0.4 | meaning changes: the floor each cycle decays toward, and the held level when `env_loop == 0` |
| `vol_fade` | Linear | 0–1 | 0.4 | **new**, replaces `vol_rel` (D4) |
| `mod_atk` | Exp | 0.001 – 4.0 s | 0.5 | |
| `mod_dcy` | Exp | 0.010 – **12.0 s** | 0.5 | current max already 12.0 |
| `mod_sh` | Linear | 0–1 | 0.4 | |
| `mod_vel` | Linear | 0–1 | 0.3 | |
| `lfo_rate` | Exp | 0.020 – 30.0 Hz | 0.6 | matches |
| `lfo_amt` | Linear/Squared | 0–1 | 0.5 | destinations change to pitch+volume; **pitch tag removed** (D7) |
| `lfo_bal` | Bipolar | −1…1 | 0.5 | now means pitch↔**volume**, not pitch↔filter |
| `lfo_dly` | Exp | 0 – 5.0 s | 0.3 | **new** — onset delay |

**Page II — Oscillators (15)**

| Gene | Curve | Range | S | Notes |
| --- | --- | --- | --- | --- |
| `a_form` `a_color` `a_noise` `a_freq` `a_mod` | as today | | | `a_freq` keeps `kFlagRatio \| kFlagPitch` |
| `b_form` `b_noise` `b_freq` `b_mod` `b_sh` `sub_am` | as today | | | `b_sh` unchanged (D10) |
| **`osc_mix`** | Linear | 0–1 | 0.4 | **rename of `balance`** |
| **`mix_mod`** | Bipolar | −1…1 | 0.4 | **new** — mod-envelope amount on the oscillator mix |
| `fm_amt` `fm_mod` | as today | | | |

**Page III — Filter and Effects (17)**

| Gene | Curve | Range | S | Notes |
| --- | --- | --- | --- | --- |
| **`flt_type`** | **Stepped** | 3 values | 0.3 | **new gene**; removes `traits.filterMode` (D8) |
| `flt_freq` `flt_q` `flt_kf` `flt_mod` `flt_sep` | as today | | | |
| **`saturate`** | Squared | 0–1 | 0.4 | **rename of `sat_drive`** |
| **`rvb_size` `rvb_len` `rvb_damp` `rvb_chor` `rvb_mix`** | as today | | | **rename of `fx_*`** |
| **`rvb_atk`** | Exp | 0 – 5.0 s | 0.3 | **new** — wet-send fade-in (and/or pre-delay), keep swappable |
| **`adj_bass` `adj_treb`** | Bipolar | ±9 dB | 0.2 | **new** — shelves |
| **`adj_pan`** | Bipolar | −1…1 | 0.2 | **new** — equal-power pan, per-voice variation on top |
| **`adj_clip`** | Linear | 0–1 | 0.1 | **new** — output soft clip |

### 3.2 The extras, kept and demoted (2nd table, not part of the 48)

| Gene | Decision | Justification |
| --- | --- | --- |
| `b_color` | **keep** | Symmetry with `a_color`; `MorphOsc` already takes a colour per oscillator; costs nothing. |
| `fx_width` | **keep** | Stereo decorrelation of the FDN is the difference between a wide organic tail and a grey one. Belongs in the extras block, not hidden inside the 17. |
| `vol_rel` | **remove as a gene** | Its function stays as a fixed release (D4/D5), derived from `rvb`-independent timing. |
| `det_spread` | **remove as a gene** | Rev 2 §7: per-voice detune is voice behaviour. Keep as a *constant* (today's 2 cents + gene-scaled) or re-add later if it proves musical. |
| `drift_amt` | **remove as a gene** | Same sentence of Rev 2. It currently drives both pitch and cutoff drift; make the depth a per-voice constant, and *if* we want it curated, put the drift *rate* into the mutation as a trait, not a gene. |

**Consequence to design honestly:** removing `drift_amt` and `det_spread` from the
`kFlagPitch` set removes two of the four things atonality currently bounds. After Stage A
the pitch-tagged set is `a_freq`, `b_freq` only, plus the pitch *component* of vibrato
(D7). `testAtonalityCeiling`'s expected numbers (0.000 / 0.177 / 0.710) will change.

### 3.3 Migration

- `mutationVersion` 1 → **2**. `state->setAttribute("mutationVersion", …)` already exists
  (`PluginProcessor.cpp:292`).
- v1 XML stores 43 gene values in table order. A `kV1ToV2GeneMap[kNumGenesV1]` table maps
  each old id to a new id (or −1), and new genes absent from v1 are filled from
  `pitchNeutral`-style neutral defaults, then `applyCouplings` runs. This is ~40 lines of
  data and one loop in `setStateInformation`.
- Add a test that constructs a v1 state blob, loads it, and asserts the 38 carried genes
  survive byte-exactly and every new gene sits inside its soft range.

---

## 4. Stages

Each stage is a commit-sized unit, has a gate, and states what it must **not** break.
Gates are `check()`-style claims in `Tests/EngineTests.cpp` unless stated otherwise.

### Stage A — Gene table to Rev 2 (no audible change intended)

**Files:** `Genetic/GeneTable.h/.cpp` (incl. `kGroupNames`), `Genetic/Curves.h` (add
`Curve::Stepped`), `Engine/VoiceParams.h/.cpp`, `Genetic/Mutation.cpp` (`applyAtonality`
gene set, `applyCouplings`, `safetyScore`, `loudnessCompensation`), `UI/GeneGrid.{h,cpp}`,
`Tests`.

**Build:** rename `balance`→`osc_mix`, `sat_drive`→`saturate`, `fx_*`→`rvb_*`; add the 10
new genes and remove the 3 demoted ones; new page-ordered grouping (`GrpEnvClock`,
`GrpAmp`, `GrpModEnv`, `GrpLfo`, `GrpOscA`, `GrpOscB`, `GrpMix`, `GrpFilter`, `GrpSat`,
`GrpReverb`, `GrpOut` + `GrpExtra`) so group-coherence matches the document's pages;
`GeneGrid` follows automatically for the rows (it builds one per entry of `kGenes[]`),
**but** `GeneGrid::resized` splits its two columns at `GrpCount / 2` (`GeneGrid.cpp:133`),
which is only sane for today's 10 groups. With page-ordered groups it must split at the
*page* boundaries instead (three pages, not a halved enum), and `GeneGrid.h` sizes
`groupHeaders` as `std::array<…, GrpCount>`.

Wire every new gene into `resolveVoiceParams` but make each one a **no-op or near-neutral**
at first: `env_kf` → 0 effect, `vol_fade` → 0, `lfo_dly` → 0, `mix_mod` → 0, `flt_type` →
drives `p.filterMode` from the gene (only real change), `rvb_atk` → 0, `adj_bass/treb` →
0 dB, `adj_pan` → centre, `adj_clip` → off. This keeps the diff reviewable and the gate
"nothing got louder/quieter or brighter".

**Gate:**
- `testCurves` extended to every new gene (round-trip).
- `testGenome`: distance 0 == seed; deviation monotone; all genomes in [0,1]; the
  "usable branches" count must not drop below its current 720/720 — that number is now a
  real regression signal because the guard rails (design doc §5.6) have to learn the new genes.
- New `testTableMatchesDoc`: asserts the table's first 48 ids, in order, equal the Rev 2
  list, and the extras block is marked. This is the contract test that stops the vocabulary
  drifting again.
- A/B render comparison of one fixed seed before/after the rename: RMS and centroid within
  tolerance except where `flt_type` was already implied.

**Must not break:** `testPitchStability` (worst movement ≤ 15 cents), bit-exact
reproducibility, 44.1/48/96 kHz renders.

### Stage B — The shared looping envelope engine (§6.1.1) — the core of this plan

**Files:** `Engine/Dsp.h` (new `LoopEnv` replacing the amp/mod `Env` pair for this purpose;
keep `Env` for the fixed release), `Engine/Voice.{h,cpp}`, `Engine/VoiceParams.*`, `Tests`.

**Build (design in code terms):**

```
T          = envTime * exp2( -envKf * (note - 60) / 12 )      // env_kf, from VoiceParams
phase     += blockSeconds / T                                  // per voice, per control block
cycleIndex = number of completed cycles (int, saturating)
p          = tiltWarp(min(phase,1), envTilt)                   // reuse tiltTriangle's duty warp
peak       = pow(1 - volFade, cycleIndex)                      // amp envelope only (D4)
volEnv     = peak * adShape(p, volAtk, volDcy, volSus)         // adShape: rise, fall to volSus floor
modEnv     = adShape(p, modAtk, modDcy, modSh)                 // no fade (figure 1b: level peaks)
if (phase >= 1 && noteHeld && envLoop > 0) { phase -= 1; ++cycleIndex; }   // D3 depth semantics
note off: stop looping, run the fixed 180 ms release (D5)
```

`env_loop == 0` must give a plain one-shot that settles at `vol_sus` — the document's
"behaving like an ordinary envelope" case, and the natural bridge from today's ADSR.
`adShape` must be a single function used by both envelopes with different arguments, so
`mod_sh` and `vol_*` cannot drift apart.

What is deleted: `ampEnv`'s ADSR stage machine as the amp path, `loopEnvOneShot`, the
`loopMod` triangle + `tiltTriangle` pair. What replaces the "loop as a mod source" idea:
**`modSource` trait is removed** — the loop *is* the envelope engine now, not an
alternative modulator. (That is the Rev 1 → Rev 2 substitution the doc calls out: "the four
`env_*` knobs sit above both envelopes rather than inside either one".) The structural
`modSource` slot is freed and re-pointed at §6.3's routing switch (Stage D).

**Gate — `testLoopEnvelope`, new, the most important gate in this plan:**
1. **Cycle exactness:** at `env_loop = 1`, `env_tilt = 0`, `vol_fade = 0`, the number of
   envelope peaks in a 4 s held note at a given `env_time` equals `floor(4 / T)` ± 1,
   measured by counting local maxima of the rendered envelope (not by trusting the phase
   counter).
2. **Key follow:** `env_kf = +1`: C5's measured cycle count is exactly 2× C4's; `env_kf =
   −1` gives ½. (This turns D3/D6-style claims into measurements, the way the DR2 gates do.)
3. **Fade:** peak amplitudes of successive cycles fall geometrically with ratio
   `1 − vol_fade` within 2 %.
4. **One-shot:** `env_loop = 0` → exactly one peak, then a floor equal to `vol_sus` within
   1 %.
5. **Note-off:** after note-off the envelope completes no new cycle and reaches silence
   within the fixed release ±10 ms.
6. `testPitchStability` still green — the loop must not reach pitch (this is the exact
   regression class of the "note changes pitch when I play it" bug).

**Stop condition:** the six checks above, at 44.1 and 96 kHz, are the acceptance for §6.1.1.
Anything the document marks `[unknown]` (D3, D4, D5) stays behind one function each.

### Stage C — LFO: destinations and onset delay (§6.2)

**Files:** `Engine/Voice.cpp`, `VoiceParams.*`, `Tests`.

**Build:** tremolo instead of filter modulation.

```
lfoPitch = lfoV * lfoAmount * env::pitchBalance(lfoBal)     // cents, same 0.5 max as today
lfoAmp   = lfoV * lfoAmount * env::ampBalance(lfoBal)       // multiplies the amp path
tOnset   = ramp 0→1 over lfo_dly from note-on, then 1       // lfo_dly
```

Per-voice phase/shape randomisation stays (it is "voice-level life", §7). Filter tremolo is
not in the fixed path (D6).

**Gate — `testLfoDestinations`:**
- `lfo_bal = −1`: amplitude modulation measured, pitch modulation **zero** (< 2 cents by
  autocorrelation); `lfo_bal = +1`: the reverse. Mid-balance gives both at the expected
  proportions.
- `lfo_dly`: measured modulation onset time within 10 % of the gene, and no vibrato in the
  first `0.5 × lfo_dly`.
- Vibrato depth stays within the atonality ceiling after D7's retagging.

### Stage D — `mix_mod` + the groundwork for §6.3's routing matrix

**Files:** `Engine/Voice.{h,cpp}`, `VoiceParams.*`, `Genetic/Mutation.{h,cpp}`.

**Build, two halves:**
1. `mix_mod`: modulate the A/B mix around the gene value — `balance + mixMod * modMix *
   kMixDepth`, clamped, smoothed at control rate like every other target. `kMixDepth` is a
   named constant (start 0.35 of the pan range) so it is one line to tune.
2. Replace the removed `modSource` trait with a **routing trait** per §6.3: a small fixed
   list of `{source, destination}` pairs per branch (start: 6 slots), where today's set is
   `{mod→flt}` and the rare additions are `{lfo→flt}`, `{loop→mix}`, `{mod→rvb_damp}`.
   Cap active routings at 6. Keep the existing per-destination gene weights as the
   *amounts*; the matrix chooses *which source is allowed*, which is what "structural"
   means here.

**Gate:**
- `testMixMod`: with a large `mix_mod`, the measured A-vs-B energy balance moves across the
  held note; with `mix_mod = 0` it does not move at all.
- `testRoutingCap`: a branch's active routing count never exceeds 6; routings are a
  deterministic function of `(seedId, branch)`; they only appear past the same
  `distance³ × rate` gate as the other structural changes, and stay flipped as distance
  grows (the existing monotonic-threshold trick).

### Stage E — `flt_type` as a stepped gene, and crossfading it

**Files:** `Genetic/Curves.h` (`Curve::Stepped`), `GeneTable`, `Engine/Voice.*`, `Tests`.

**Build:** `mapGene` on a stepped gene returns a step index; `Voice` keeps three filter mix
weights (`lpMix/bpMix/hpMix`) with a target from the gene and ramps between them over
`kTypeXfadeMs` (~20 ms) at control rate — `TrapezoidSVF::process(x, lpMix, bpMix, hpMix)`
already accepts weights, so this is the cheap version of "crossfade type changes".
Remove `traits.filterMode`.

**Honest note:** because a voice captures its genome at note-on, and `flt_type` only changes
as *distance* changes, this crossfade is **not audible** unless D9 (re-resolving held notes
with a ~50 ms crossfade) is implemented. Implement the ramp anyway — it costs ~10 lines and
makes D9 a policy switch instead of a feature.

**Gate — `testFilterTypeStep`:** the gene maps to exactly 3 values, all reachable, each
produces a measurably different spectrum (centroid ordering LP < BP < HP on a fixed
stimulus), and a simulated step change shows no discontinuity larger than X samples of
slew (compare the rendered signal against the same render at constant type).

### Stage F — Reverb onset `rvb_atk` and the `adj_*` output stage (§8, §8.1)

**Files:** `Engine/MasterFx.{h,cpp}`, `Engine/VoiceParams.*`, `Tests`.

**Build:**
- `rvb_atk`: fade the wet send in over `rvb_atk` from note-on (the "tail blooms" reading),
  implemented as a per-block-ramped gain on the send, not per sample. Keep pre-delay as the
  alternative reading behind one `if` (Rev 2 says keep it swappable).
- A new `OutputStage` (in `Engine/`, JUCE-free): low shelf + high shelf (biquad shelf,
  ±9 dB, coefficients derived from `sampleRate` at block rate), equal-power pan (per-voice
  jitter *added on top*, as today), and a soft-clip limiter (`tanh`-family, drive from
  `adj_clip`). Placement: after the reverb, per page order (§7's `[inferred]` placement).
- Guard rail interaction: `loudnessCompensation` must account for shelf gain (it already
  accounts for resonance, FM, sub, noise, drive). `adj_bass` at +9 dB is 9 dB of loudness
  the current compensation cannot see.

**Gate:**
- `testOutputStage`: measured low-shelf response at 60 Hz and high-shelf at 8 kHz match the
  requested dB within 0.5 dB at 44.1/48/96 kHz; pan is equal-power (L²+R² constant);
  `adj_clip` at max keeps the peak under a stated ceiling with no NaN.
- `testRender` levels: 12 branches at distance 0.85 still land inside ~1 dB of each other
  **with `adj_*` mutating freely** — this is the check that the compensation upgrade worked.
- Reverb: no metallic ringing at defaults — quantify as the max spectral-peak-to-average
  ratio in the tail, or (Stage I) with the flatness metric. Until Stage I, at minimum:
  tail decays to −60 dB in `rvb_len` ±10 % and no tail resonance above the input's.
- `rvb_atk`: measured wet/dry ratio at t=0 vs t=`rvb_atk` differs by the expected amount.

### Stage G — Voice quality items the document asks for (§7) and the current code admits

Not strictly Rev 2 *changes* — they are Rev 1 requirements we have not met. Listed here so
one pass over the voice covers everything.

1. **FM oversampling** (2× on the FM path, not the whole chain). The document says
   "consider 2–4x"; aliasing is called out as the #1 reason digital synths sound cheap.
2. **A real band-limited form morph.** Today's `a_form` is an equal-power crossfade of four
   PolyBLEP shapes and `a_color` adds an octave partial — both self-declared placeholders.
   Cheapest honest upgrade: keep PolyBLEP per component but morph the *component amplitudes*
   against a Nyquist-limited harmonic budget instead of crossfading waveform objects.
3. **Voice stealing**: fade the stolen voice out (Implementation-Status §4.7) instead of
   restarting it. §7 asks for "release-aware stealing and a short fade".
4. **DC blocker placement** — today it is after the filter; §5.6 asks for it after FM/sync
   too. Cheap; do it while in the file.

**Gate:** the existing `testRender` gains an **aliasing estimate** (energy above the expected
harmonic series on a high note) which Stage I's tooling produces; the FM-path check must
show that estimate drop after (1).

### Stage H — MIDI and host integration (§9)

**Files:** `Source/PluginProcessor.cpp`, `Engine/Synth.cpp` (pitch bend, sustain), `Tests`.

Pitch bend (with `PitchWheelRange`), sustain pedal (release-aware voice aging so a damped
note still steals), MIDI receive channel, `VelocityCurve` (three monotonic curves), program
change → presets, and a persisted CC map. Also: `producesMidi()` is currently false, which
blocks any future "plant → MIDI out" idea (optional, §9).

**Gate:** `testNoteBookkeeping` extended — sustained notes survive steal, pitch bend moves
the measured pitch by the configured semitones (autocorrelation, the existing method), a
stray CC does not create a held note.

### Stage I — The tuning tool the whole project needs (§13) — *build it early, not last*

**Files:** new `Tools/` or `Tests/render_main.cpp` (a second target in `Tests/CMakeLists.txt`,
e.g. `GenoMosh_Render`), reusing `gmosh::Synth` which is already JUCE-free for exactly this.

**Deliverable:**

```
GenoMosh_Render --seed <id> --branch <n> --distance <d> --note <n> --vel <v> --out file.wav
GenoMosh_Render --batch <dir>            # 12 branches × distances 0, .25, .5, .75, 1
```

plus a metrics report per render: peak, RMS, loudness relative to the seed branch, silence,
NaN/Inf, DC, **spectral centroid**, **spectral flatness**, and the **aliasing estimate**.
A 512/1024-point FFT in the test target is enough; the engine's own `Dsp.h` need not grow.

**Why this is Stage I and not last:** the design document's §16 priority 1 is *curating the
gene space*, and Implementation-Status §4.1 says the gene ranges have never been listened
to. Stages B–F all change ranges and depths; without metrics they are tuned blind, and the
`usable branches` count is too blunt to tell "harsh" from "dull". If the operator wants to
reorder, this is the stage to pull forward.

**Gate:** batch run produces N WAVs + one metrics table; the table is what makes
`applyCouplings` and `safetyScore` tunable instead of guessable. Bit-identical reruns.

### Stage J — Documentation and housekeeping reconciliation

- `Docs/Implementation-Status.md`: a section 10 "Rev 2 alignment" recording what changed,
  measured; the "43 genes" claim and the §2 deviations table must be updated or superseded.
- Design doc addenda for the four deviations we are keeping (D11 atonality ceiling, D12
  rotation removed, D13 `softCompress`, D14 directions-not-genomes). These belong **in the
  design document**, because it is the contract; this plan file only tracks the work.
- `Pictures/2026-10-22-0854xx/0855xx_hyprshot.png` have **no sidecar `.txt`** — the project
  convention is one `.txt` per screenshot describing what it shows. Write them.
- `README` / build docs if the gene count or UI pages change visibly.

---

## 5. Verbose design notes — the details that will actually bite

Long-form reasoning for the parts of Rev 2 that do not translate one-to-one into this
codebase. Anything here marked *(undecided)* is not answered by the document and is not
answered by this plan; it is written down so it is found now rather than in a listening
session.

### 5.1 Attack and decay are absolute seconds; the cycle is not

Rev 2 gives `vol_atk`/`vol_dcy` absolute exponential ranges (≈1 ms–5 s) that shape what
happens *inside* a cycle whose length `T` is 20 ms–10 s, and says explicitly: "if attack
plus decay is longer than `T`, the cycle is cut off by the restart". That is a legal
reading, and it means three individually-legal genes can produce silence: `env_loop > 0`
with `env_time` near its floor and `vol_atk`/`vol_dcy` near their ceiling leaves the amp
envelope at a few percent of peak, forever.

- `applyCouplings` needs a new term: if looping is on and `vol_atk + vol_dcy > k·T`
  (start `k = 1.5`), pull one of them. Which one changes the music — raising `env_time`
  gives a slow swell, lowering the times gives a fast strummed blip. *(undecided)* Default:
  raise `env_time`, because `vol_*` is what reads as "the envelope" in the UI.
- `randomizeGenome` biases `vol_atk ≤ 0.35` of range; against a 5 s maximum that is 1.75 s,
  already longer than the shortest cycle. The bias must become **relative to `env_time`**,
  not a fraction of range.
- `safetyScore` needs the same term, *and* a second one Rev 2 names explicitly: "a loop
  whose `vol_fade` has silenced it before the note ends". That is a different failure from
  every silence check we have — it is silent *later*, not at the start. Total energy over a
  held note is roughly `peak·(1-(1-fade)^n)/fade` with `n = noteLength/T`; the check has to
  be made at a nominal note length (say 2 s) or it cannot exist. `testRender` renders a
  fixed note, so it would not currently catch it either.

### 5.2 Two velocity controls already overlap

`VelSens` (macro) "scales how much velocity affects amp **and the modulation envelope**"
and `mod_vel` (gene) is "velocity to modulation depth". Today both act: `p.velocity` from
VelSens multiplies the output, `modScale` from `mod_vel` scales `aMod`/`bMod`/`fmMod`/
`filterMod`. That is what the document asks for — but once the amp path is a loop engine,
velocity can also act on a third place: the depth *below the `vol_sus` floor*, i.e. whether
a hard key strikes louder or strikes a *deeper envelope cycle*. *(undecided)* Default: keep
the current two; do not add a third until it is heard.

### 5.3 `rvb_atk` cannot be a per-note fade in a master bus

`MasterFx` is one instance for the whole plugin; `rvb_atk` is a per-voice onset. Three ways:

| | Approach | Cost | Wrongness |
| --- | --- | --- | --- |
| (a) | Per-voice wet sends: split dry/wet per voice, apply a per-voice onset ramp on the send | A wet buffer pair per voice, or a second accumulation pass — real memory traffic and a change to how `Synth` sums voices | Nothing. This is the correct one. |
| (b) | Bus-level ramp keyed to the most recent note-on | ~5 lines | Chords and arpeggios: the tail responds to *when you last played*, not to the note. |
| (c) | `rvb_atk` as **pre-delay** on the FDN input | One `VariableDelay` | No bloom — the wet arrives at full strength, only later. |

Rev 2 offers "(and/or pre-delay)", so (c) is sanctioned. Default: **(c) first**, and (a) if
the bloom is what we turn out to want. *(undecided, and it is the only item in this plan
that changes the plugin's audio graph.)*

### 5.4 `adj_pan` versus per-voice pan, and what a chord does

There is already a per-voice random ±0.30 pan (voice-level life). `adj_pan` is a *patch*
value, and since it is a gene it is a *branch* value. Sum rule is easy
(`angle = (adjPan·0.5+0.5 + jitter)·0.5π`, equal power), but the consequence is not:
`adj_pan` differs per branch, so the notes of a chord land apart by the difference of their
branches' values. That is either spread or smear. If it smears, the fix is a new plant-level
coupling that bounds the **variance** of `adj_pan` across the 12 branches, not a clamp of
the gene. Add an L+R mono-compatibility check to the Stage I metrics — it is cheap and it
is the measurement that settles this.

### 5.5 `flt_type`: a gene, stepped, and the monotonicity gate

Rev 2 lists `flt_type` among the 17 page-III genes *and* §5.4 says to step it "rather than
continuously" with a crossfade. Both are satisfied by `Curve::Stepped`: continuous in gene
space, quantised to 3 values after mapping, so it visibly jumps as a branch grows. **Note
for the test author:** `testGenome`'s "deviation grows monotonically with distance" check
must be asserted in *gene space*. The mapped value of a stepped gene is a step function and
is legitimately flat, then discontinuous. Without that distinction a correct implementation
fails the gate — the same class of trap as a lattice random walk's parity.

The crossfade itself is nearly free (`TrapezoidSVF::process` already takes lp/bp/hp weights)
but is **not audible** while voices capture their genome at note-on; see D9.

### 5.6 Removing the `modSource` trait does not lose a structural mutation

Today's four traits: `filterMode` (becomes a gene, D8), `fmDirection` (keep — §5.4 names
"switch the FM direction"), `oscBMode` (keep — it is `b_sh`'s mode, D10), `modSource`.
The last one is the Rev 1 stand-in for §6.3's routing switch; Rev 2 removes the idea of
"the loop envelope as an alternative modulator" because the loop *is* the envelope engine
now. Stage D replaces that trait slot with a routing mask. Nothing from §5.4 is lost.

### 5.7 `env_kf`'s sign is the opposite intuition to `flt_kf`'s

Rev 2: `T = env_time · 2^(−env_kf·(note−60)/12)` → positive means higher notes cycle
*faster*. `flt_kf` in this code is "octaves of cutoff deviation", positive meaning higher
notes open *up*. Both stay as written; the comments must say so in those words. The
`testLoopEnvelope` key-follow check pins the sign by measurement (C5 = exactly 2× C4's
cycle count), so a later flip becomes a failure instead of a silent musical change.

### 5.8 `loudnessCompensation` and `safetyScore` are gene tables in disguise

Ten new genes means each one either appears in those two functions or is declared, in a
comment, not to affect loudness. Without that list, "why is this branch louder" stops being
answerable. Disposition of the new genes:

| Gene | Loudness effect | Notes |
| --- | --- | --- |
| `vol_fade` | **reduces** average level (later peaks shrink) | compensate in the same pass as `vol_sus` |
| `lfo_amt` (tremolo) | peak up, average ~unchanged | RMS-neutral; affects the peak guard, not the RMS one |
| `lfo_dly` | none | |
| `env_kf` / `env_tilt` | small (duty cycle) | ignore at first |
| `mix_mod` | small | only if the mix moves toward a louder oscillator |
| `flt_type` | **large** | a bandpass or highpass at the same cutoff/Q is far quieter than a lowpass — **the current compensation does not see filter mode at all**, and it never could before, because filter mode was not in the genome |
| `rvb_atk` | none as pre-delay; **reduces** wet as a send fade | depends on 5.3 |
| `adj_bass` / `adj_treb` | direct, up to ±9 dB | the largest unmodelled gain in the whole chain |
| `adj_pan` | none per channel; affects L+R | see 5.4 |
| `adj_clip` | **raises** average (squashes peaks) | and it is the one the reference marks with an asterisk |

The `flt_type` row is the one that will actually bite, because it turns level compensation
into a function of a *discrete* choice.

### 5.9 The document's "6 dB" is a ceiling, not our gate

Design doc §5.6 asks for branches "within about 6 dB of the seed". The current
compensation lands them within ~1 dB. Do not loosen the test to 6 dB — keep 1 dB as the
regression gate, and let 6 dB be the distance a plant is *allowed* to approach on purpose
(for instance when `saturate` or `adj_clip` are deliberately high).

### 5.10 What Stage A's "no audible change intended" means exactly

It is a falsifiable claim, and it is the cheapest safety net in this plan: render one fixed
seed × 12 branches × 3 distances before and after the table change, and require the RMS and
spectral centroid to match within a small tolerance for every branch **except** where
`flt_type` now resolves a filter mode that the old trait chose differently. A mismatch that
is not on that exception list is a bug in the rename, not a sound improvement.

---

## 6. Order, dependencies, and what to skip

```
A (gene table) ──┬──> B (loop envelope) ──┬──> C (LFO dest) ──> D (mix_mod + routing)
                 │                        │
                 └──> E (flt_type)        └──> F (rvb_atk + adj_*)  ──> G (voice quality)
                 │
                 └──> I (render/metrics harness)  [can start in parallel with B]
J (docs) runs alongside every stage.
H (MIDI) is independent of all of the above — fill-in work, lowest priority.
```

**Recommended sequence:** A → **I** → B → C → F → E → D → G → H.
Rationale: A and I together make every later change measurable; B is the single change that
moves the sound closest to the reference; F is next because the missing `adj_*` stage and
loudness compensation interact with every guard rail; E and D are mechanical after A.

**Explicitly not in this plan:** the plant UI rewrite (§12's rotation handle, mod-wheel arc,
hover/drag states, hue from genes), presets/browser UI, §5.6.1's crossover/breeding, D9's
held-note re-resolution, and anything `[unknown]` in Rev 2.

---

## 7. Regression risks worth naming

| Risk | Where it bites | Mitigation |
| --- | --- | --- |
| Changing the table invalidates every saved plant | `setStateInformation` | D15: `mutationVersion = 2` + explicit v1 map + a load test |
| Guard rails learn new genes late → silent/unusable branches | `safetyScore`, `applyCouplings` | The 720/720 usable-branch number is a hard gate from Stage A on; §5.6's "a loop whose `vol_fade` has silenced it before the note ends" is a *new* silence case to add to both |
| `adj_*` shelves break the ~1 dB level compensation | `loudnessCompensation` | Stage F gate measures it; shelves are the largest unmodelled gain in the chain |
| The pitch policy regresses (it has before, twice) | `Voice::updateControlTargets` | `testPitchStability` runs second in the harness and stays; the loop engine must not reach pitch |
| Control-rate smoothing budgets grow (more targets to ramp) | `Voice::kCtrlPeriod` paths | New targets join the existing ramp set; do not add per-sample work |
| `PlantSnapshot` grows (36 direction slots × genome) and the UI copies it at 30 Hz | `PlantEngine.h`, `PlantView::update` | Measured today at 43 genes: `sizeof(Genome)=172`, `sizeof(BranchDirection)=192`, `sizeof(PlantSnapshot)=7120` → **21 360 B across the triple buffer**. At 48 genes ≈ 23 kB, at 50 (keeping `b_color`/`fx_width`) ≈ 25 kB. Still irrelevant for the audio thread, but the known weak point "the view only needs hue/hash" should actually be fixed in Stage A, and these sizes should be re-measured after the table change rather than estimated. |
| Two documents disagreeing about the design | `Docs/` | Stage J; this file never restates the design, it plans against it |

---

## 8. What to ask the operator, in one list

Blocking only where marked **(B)**; everything else has a default in §2.

1. **(B) D4/D5** — is `vol_fade` the loop fade or the release? Does the reference's amp
   envelope have a release stage, and does looping stop at note-off? (Design doc §15 q9.)
2. **(B) D3** — `env_loop`: repeat count, loop region, or re-excitation depth? (§15 q10.)
3. **(B) D1/D2** — confirm the demotion of `vol_rel`, `det_spread`, `drift_amt`, and whether
   the genome must be *exactly* 48 (`b_color`, `fx_width` are the ones in question).
4. **(B) D15** — v1 presets: migrate, or refuse to load?
5. D6 — is dropping filter tremolo from the *default* routing acceptable (kept as a rare
   structural routing)?
6. D7 — confirm that atonality must not suppress tremolo.
7. D9 — should dragging a branch re-resolve a *held* note (with a ~50 ms crossfade)? This
   is the only thing that makes §5.4's crossfade requirement observable.
8. D11–D14 — confirm these four are design-doc edits, not code changes, so I can write the
   addenda.
9. §15 q4 — polyphony/CPU budget (16 voices today, no CPU measurement exists).
10. §15 q5 — should branch lengths be MIDI-learnable? (They are host-automatable today.)
11. This plan §5.1 — when a cycle is shorter than `vol_atk + vol_dcy`, should the guard rail
    raise `env_time` (slow swell) or lower the times (fast blip)? Both are legal; they sound
    completely different. *(blocking for Stage B's coupling)*
12. This plan §5.3 — is `rvb_atk` a pre-delay, or do we change the audio graph to get a real
    per-voice wet bloom? The latter is the only option that alters how `Synth` sums voices.
    *(blocking for Stage F)*
13. This plan §5.4 — is it acceptable for different notes of a chord to sit at different pans
    because `adj_pan` is a per-branch gene? If not, the fix is a variance-bounding coupling,
    which quietly reduces what that gene can do.

---

## 9. Definition of done for this plan

- `GENO_MOSH_GENE_LIST`'s first 48 entries match Rev 2 §4.2 by id, curve and page group,
  and `testTableMatchesDoc` proves it.
- The amp and modulation envelopes come from one cycle clock with `env_kf`, `env_tilt`,
  `env_loop`, `vol_fade`; there is no ADSR path left in `Voice`.
- The LFO's destinations are pitch and volume with `lfo_dly`; no fixed LFO→filter path.
- `mix_mod`, `flt_type` (stepped gene), `rvb_atk`, and the four `adj_*` genes exist, are
  reachable from mutation, and are modelled by the level compensation.
- The offline render harness emits WAVs plus level/spectral/aliasing metrics, and the
  12×5 batch over a handful of random seeds is the first thing that was ever *measured*
  about the gene ranges.
- `ctest` green at 44.1 / 48 / 96 kHz, bit-identical reruns, `testPitchStability` and the
  note-bookkeeping tests untouched in intent.
- `Implementation-Status.md` carries a dated Rev-2 alignment section, and the design
  document carries addenda for the four kept deviations.
