# Design Document Revision 3 — Change Log and Rev 2 → Rev 3 Delta

*Companion to `Docs/Genetic Synth - Design Document for Local LLM.md` (Revision 3, 2026-10-29).*
*Nothing here is a specification. This file records what changed, why, and what the code and the
other plan documents have to do about it.*

Everything attributed to the reference below is what it documents about itself — the help text of each
gene, its tutorial, its parameter list, its patch files — together with how it behaves when driven
from a host.

---

## 1. Why Revision 3 exists

Revision 2 (2026-10-29, commit `aff2dae`) was written from screenshots of the reference product's
three gene pages. Revision 3 reads the reference itself instead: **it documents every one of its
genes** with a short description of what that gene does, it has a tutorial describing every global
parameter, and its randomise/clone/wheel behaviour is directly observable. So most of Revision 2's
`[inferred]` and `[unknown]` markers could be replaced with described behaviour.

Net result: **Rev 2's 48 gene names, and their three pages (16 + 15 + 17), were exactly right.**
Eighteen of their meanings were wrong.

Two structural corrections were also needed. Rev 2 documented only v1's 21-parameter host surface;
v2 exposes **all 48 genes as parameters 26-73** (140 parameters on the VST3 face),
so "genes are not host-visible" is a v1-only statement. And Rev 2's macro table missed that
`Effect` and `Release` are **multi-valued** (`Effect` = effect mix *and* panning; `Release` = time
*and* reverb gating) and that `Volume` drives a **saturating limiter**.

---

## 2. Gene semantics: Rev 2 → Rev 3

| Gene | Rev 2 said | The reference describes it as |
| --- | --- | --- |
| `env_time` | cycle length, 20 ms-10 s | **total duration of the sound**; ≥0.9 = infinite, stopped only by key-up |
| `env_loop` | loop *amount* (0 = one-shot) | a **loop period**; longer than `env_time` ⇒ no looping |
| `vol_atk` `vol_dcy` `mod_atk` `mod_dcy` | attack/decay **times** | **envelope curvature**, 0.5 = linear |
| `vol_sus` | sustain floor | sustain; **<0.5 = no sustain**, 1.0 = decay inaudible |
| `vol_fade` | peak fade across loop repeats | a **second fade after the attack**, active only above 0.75 |
| `env_kf` | key follow of cycle length, bipolar | key follow; **above 0.5 shortens** the envelope on high keys |
| `mod_sh` | modulation envelope *shape* | **sample-and-hold rate**; only very high values activate; higher = *slower* |
| `mod_vel` | velocity → mod depth | confirmed; **<0.1 = no velocity** |
| `b_sh` | "[inferred, uncertain]" sync/S&H/shape | **S&H rate for B**; below 0.5 synced to B's frequency "for a consistent timbre across pitches" |
| `lfo_amt` | plain depth, "*" | bipolar: 0.5 = none; **below 0.5 modulates A and B in opposite directions** (v1: below 0.5 = off) |
| `lfo_bal` | pitch/volume split **[inferred]** | confirmed: 0.0 vibrato only, 0.5 both, 1.0 tremolo only |
| `lfo_dly` | onset delay | confirmed; **none below 0.5** |
| `a_form` | "sine through to bright shapes" | sine **0.0** → saw **0.57** → square **0.81** → pulse **1.0** |
| `a_color` | pulse width / spectral tilt / formant | **colour of the noise: brown (muddy) → white (sharp)** |
| `a_freq` | semitone/ratio offset vs the played note | **pitch of A on a 7-step octave grid (−3…+3)**; it *is* the patch pitch; B's pitch and both filter cutoffs are **linked to it** |
| `a_mod` `b_mod` | "mod-envelope amount applied to A/B" | **bipolar *pitch* modulation from the envelope**, 0.5 = none — exactly the genes Atonality biases |
| `sub_am` | "sub level and/or AM amount" | **AM of B one octave lower**, as a mix |
| `osc_mix` | linear A/B mix | **non-linear, 50/50 at ≈0.79**; 0 = only A, 1 = only B; default 0.7937 |
| `fm_amt` | "B modulating A" | confirmed — "A modulated by B", bipolar, 0.5 = none |
| `flt_type` | stepped or morphing LP/BP/HP | **four discrete topologies**: 0.0 bandpass, 0.33 dual serial LP, **0.66 dual parallel LP (default)**, 1.0 notch |
| `flt_freq` | absolute exponential cutoff, 0.75 default guess | cutoff **relative to A's frequency** (a tracking filter); default 0.75 |
| `flt_sep` | dual-filter spread, 0 = none | spread of the two filter cutoffs, **0.5 = no separation** |
| `flt_kf` | linear key follow in octaves | **soft-railed**: <0.25 nothing, >0.75 follows the keyboard exactly |
| `saturate` | post-filter soft clipper | **per-voice** warm distortion (new gene in v2; v1's only distortion was the unnamed limiter behind `Volume`) |
| `rvb_atk` | reverb onset / wet-send fade-in | a **reverb-amount envelope**, active above 0.66 |
| `rvb_size` | mutable room size | **growth-exempt**, "because it would cause disruption of the audio" |
| `rvb_mix` | wet/dry | confirmed, and "*the mix can be further adjusted with the Effect slider*" |
| `adj_bass` `adj_treb` | ±9 dB shelves | shelves **±24 dB at 100 Hz / 6 kHz**, growth-exempt |
| `adj_pan` | global equal-power pan | **branch panning amount** (spread across branches), reduced by lowering `Effect`; growth-exempt |
| `adj_clip` | output soft clip, "*" | **master soft clipper after the master volume**; growth-exempt |
| `fx_*` | "renamed from the first draft" | confirmed: `fx_mix/len/damp/chors/size → rvb_mix/len/damp/chor/size`, `balance → osc_mix` |

Unchanged from Rev 2 (right first time): `a_noise`, `b_noise`, `b_form`, `b_freq` (relative to A,
default 0.6875), `fm_mod`, `mix_mod`, `flt_q`, `rvb_len`, `rvb_damp`, `rvb_chor`, `env_tilt`
(direction now specified), `lfo_rate`, and the LFO's **pitch + volume** destinations.

## 3. Concepts Rev 3 added that Rev 2 did not have

- **Growth-exemption as gene metadata** (`kFlagNever`). v1 exempted one gene (`fx_size`); v2 documents
  five (`rvb_size`, `adj_bass`, `adj_treb`, `adj_pan`, `adj_clip`). The flag already exists in
  `Source/Genetic/GeneTable.h` — it now has real data.
- **The neutral-at-0.5 convention** as a first-class rule, with plain evidence in the factory state: every gene defaults
  to 0.5 except `osc_mix`, `flt_type`, `flt_freq`, `b_freq`.
- **Curation mechanisms §5.7.1-9** — neutral-at-0.5, linking, activation gates/dead zones, soft rails,
  deliberate quantisation, growth-exemption, non-linear musical curves, generation-time `converge()`
  couplings, and name/word generation. Design doc §5.7 is the payload of this revision.
- **Two sample-and-hold modulators** (`mod_sh`, `b_sh`) as a third modulation source family.
- **Reverb gating** from the second value of the `Release` macro — an articulator, not a reverb control.
- **Branch→pan spread** (`adj_pan` + `Effect`) as a source of movement across the keyboard.
- **Per-branch `volume` and enable** from v2's patch format.
- **Atonality's real mechanism**: `converge(x, at) = x += (0.5 − x)·(1 − at)` applied to `a_mod` and
  `b_mod` at genome-generation time.
- **Seed codes and generated patch names**: the reference encodes seeds as short base-32 strings over a
  17-character alphabet (`23456789abcdefghjkmnopqrstuvwxyz`) and generates patch names from a word list.
- **A golden-file regression protocol** for the test harness, with a seeded note-density generator
  (§13 of the design doc).
- **Three version axes** (product / engine-model / growth-algorithm) instead of one, matching
  `format: 28` vs `implementation: 29` in v2.

## 4. Questions Rev 2 asked that are now answered

| Rev 2 question | Answer |
| --- | --- |
| Q1 TriggerA-H | The reference's own CC-bound **actions**: Undo, Redo, Previous Patch, Next Patch, Plant Seed |
| Q2 `b_sh` | S&H rate for oscillator B, note-synced below 0.5 |
| Q3 Is the wheel a global morph scalar? | It **lerps** every branch toward full growth: `branch = lerp(branch, 1.0, ModWheel × WheelScale)`. v2 adds `Wheel Target` (9 values) to choose what it drives |
| Q4 Polyphony / CPU budget | 16 voices (operator decision) is safe: the reference plays many voices on one core |
| Q5 Are branch lengths automatable? | They already are in the reference — they are parameters 9-20 in v1 and v2 |
| Q6 Strictly 12 branches? | Yes: one branch per semitone of one octave, selected by the played key |
| Q8 What does the asterisk mean? | Partly: `adj_clip` is in the documented growth-exempt set. `lfo_amt` is not — still open (§15 Q2 of the design doc) |
| Q9 Release stage / does looping stop at note-off / is `vol_fade` the loop fade? | `env_time` *is* the note duration and key-up stops it; `vol_fade` is a post-attack droop active above 0.75 |
| Q10 Is `env_loop` a count, depth or region? | A **loop period** |
| Q11 Is `lfo_bal` a pitch/volume split? | Yes — 0 vibrato, 0.5 both, 1.0 tremolo |
| Q12 Where do `saturate` and `adj_*` sit? | Per-voice `saturate`; master shelves/pan, `adj_clip` **after master volume** — conflicts with `Rev2-Answers` A2, see below |

## 5. Conflicts to settle with the operator (design doc §15)

1. **`saturate` vs `adj_clip` placement.** The reference's own description: `saturate` = per-voice, `adj_clip` = master after
   volume. `Docs/Rev2-Answers.md` addendum A2: the opposite (per-voice `adj_clip` pre-filter with
   asymmetry, master post-filter `saturate`). Rev 3 defaults to the reference's description and flags it.
2. **Atonality's reach.** As the reference describes it: converges `a_mod`/`b_mod` only. Rev 2 answer D7 says "atonality
   should not affect pitch movement"; D11 says it should be a blend affecting vibrato amount. D11 and
   the reference's description agree; D7 does not.
3. **The asterisk on `lfo_amt`**, plus the fact that the three gene-page screenshots in `Pictures/`
   (`2026-10-22-0854xx/0855xx_hyprshot.png`) have **no `.txt` description** — they should, per the
   project's own convention.
4. **`layers` / `Bulb Mode` / `Tempo Sync` / `Wheel Target` enumerations**: their existence is documented, their values are not.

## 6. What this means for the code and the other documents

- `Source/Genetic/GeneTable.h` is still **Rev 1's 43-gene table**. Migration (design doc §4.2
  "Migration"): rename `fx_*→rvb_*`, `balance→osc_mix`, `sat_drive→saturate`; delete `b_color`,
  `det_spread`, `drift_amt`, `vol_rel`; add `env_kf`, `vol_fade`, `lfo_dly`, `mix_mod`, `flt_type`,
  `rvb_atk`, `adj_bass`, `adj_treb`, `adj_pan`, `adj_clip`; set `kFlagNever` on `rvb_size` + `adj_*`;
  add `neutral`, `gateLow/gateHigh`, `steps`, `helpText`, `colour` to `GeneInfo`.
- Re-semantified, not just renamed: `vol_atk/dcy` and `mod_atk/dcy` become curvature exponents,
  `env_time` becomes the note duration, `*_sh` become S&H rates, `a_freq` becomes 7 octaves,
  `flt_freq` becomes a tracking ratio, `a_color` becomes a noise spectrum, `sub_am` becomes true AM,
  `osc_mix` gets the 0.79 warp, `flt_type` becomes 4 topologies, `adj_pan` becomes a branch spread.
- **`Docs/Rev3-Alignment-Plan.md`** is now the plan of record: it audits the current code against
  this revision and sequences the migration as milestones M0–M11 with gates (M0 = "gene table
  re-semantified"). `Docs/Rev2-Alignment-Plan.md` is superseded — its decision register (§2) and
  its "details that will bite" (§5) are still referenced from the new plan, with the items Rev 3
  closes, reopens or overturns marked as such.
- `Docs/Implementation-Status.md` describes the built state and stays as-is; its next entry should
  report the usable-branch ratio under the new table.
- Presets written by the Rev 1 table are unreadable under Rev 3: bump `modelVersion`, reject or migrate.
