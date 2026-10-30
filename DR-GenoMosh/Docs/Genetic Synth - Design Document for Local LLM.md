# Genetic Synth — Design Document

*Working title: GenoMosh. Audience: a local LLM (and its human operator) implementing an original, Synplant-inspired genetic synthesizer plugin in C++/JUCE.*

*Revision 3 (2026-10-29): the gene model, the envelope model, the modulation routing, the macro surface and the architecture/persistence sections were rewritten against what the reference product itself documents — the help text of every gene, its tutorial, its patch files, and its behaviour when driven from a host.*

**Rev 3 in one sentence: Revision 2's list of 48 gene *names* was exactly right, and roughly eighteen of their *meanings* were wrong.** The reference documents every one of its genes in its own interface, so most of Revision 2's `[inferred]`/`[unknown]` markers are replaced here by described behaviour. See **`Docs/Rev3-ChangeLog.md`** for the full Rev 2 → Rev 3 delta, the questions that are now closed, and the migration notes for the code.

The five changes that matter most when implementing:

1. **Envelope genes are a duration, a loop period and a set of curvatures** — not attack/decay times (§4.2, §6.1.1). `env_time` is the length of the sound.
2. **§5.7: nine curation mechanisms the reference actually uses** (neutral-at-0.5, linking, activation gates, soft rails, quantisation, growth-exemption, non-linear curves, generation-time `converge()` couplings, naming). This section is the practical payload of the revision.
3. **Growth-exempt genes**: `rvb_size` and the four `adj_*` genes must not move as a branch grows (`kFlagNever`).
4. **The macro surface is multi-valued** (`Effect` = mix + pan, `Release` = time + reverb gating, `Volume` = limiter drive) and the mod wheel *lerps* every branch toward full growth rather than scaling it.
5. **All 48 genes are host parameters** in Synplant 2, displayed as raw 0..1 — which is only sane because genes are unitless (§4.3, §9).

*Reading order for a model with a small context budget: §4.2, §5.7, §6.1.1, §7, §14.*

---

## 0. How to use this document

**Purpose.** Describe what to build, why, and where to spend effort, so an implementing model can make good local decisions without guessing the overall intent.

**Ground rules for the implementing model**

1. This is still a **clean-room, original implementation**. Everything we take from the reference is what is visible from outside it: its *behaviour* and its *vocabulary* — the words in its interface, the help text it shows, the patch files it writes, and what it does to sound and to a host. Nothing about how it is implemented inside is used here, and none of it is needed. The DSP must be designed here; do not attempt to reproduce proprietary algorithms, presets or assets. The same *kind* of result through independently designed DSP is the goal.
2. **Markers.** Unmarked statements about the reference are descriptions of what it documents about itself, or of how it behaves when driven. Two markers are used deliberately — read them before implementing:

   | Marker | Meaning |
   | --- | --- |
   | **[ours]** | A GenoMosh design decision. Not a claim about the reference. |
   | **[?]** | Unknown. Do not implement special behaviour until the operator answers (§15). |

3. **Sound quality beats feature count.** Polish the voice, the gene table and the reverb before UI or extras.
4. Real-time safety is mandatory (§10). No allocation, locks, or I/O on the audio thread.
5. When unsure, build the smallest testable thing, render it offline, and report measurements and what you assumed.

**Provenance note.** Revision 2 was written from screenshots of the reference's three gene pages. The three gene-page screenshots in `Pictures/` (`2026-10-22-0854xx/0855xx_hyprshot.png`) still have no `.txt` description; Rev 3 does not depend on them, and where a screenshot and the reference's own documentation disagree, **the documentation wins**. Two places where they conflict are flagged explicitly ([?]): the asterisk marks on `lfo_amt`/`adj_clip`, and the placement of `saturate` vs `adj_clip` (see `Docs/Rev2-Answers.md` addendum A2).

**Out of scope for now:** audio-to-patch matching ("Genopatch"-style analysis), machine learning, MPE, cloud features, and the reference's `Bulb Mode` / `layers` feature group (present in its patch format and parameter list, semantics unresolved — §15).

---

## 1. Product vision

A synthesizer whose sound design happens by *exploration*, not by editing knobs. The player holds a **seed** (a complete patch) and grows **branches**, each a mutated relative of the seed, mapped to musical notes. Playing notes means playing different relatives of one sound. The further a branch grows, the further it strays.

**The target feel**

- Sounds are **alive**: slow drift, interacting modulators, subtle instability, evolving tails. This project's own direction (`Docs/Morphing synth.md`) is *more* movement than the reference product, so our modulation section is deliberately richer.
- Sounds are **related but different**: every branch shares the seed's character at small distances and becomes unpredictable at large ones.
- Exploration is **safe**: random does not mean broken. Silence, clipping, harsh resonance and runaway feedback are filtered out by design.
- Results are **musical by construction**: the parameter space is curated so that most of it is usable.

**Key insight to hold onto:** the genetics is a navigation system over a parameter space. The *character* comes from (a) the synth engine and FX, and (b) the curation of the parameter space: ranges, curves, couplings, dead zones and sensitivities. A mediocre mutation scheme over a great space sounds good. A great mutation scheme over a bland space does not.

**What Revision 3 adds to that insight.** The reference's curation is no longer a guess — nine concrete mechanisms are set out in §5.7. Copying those mechanisms is cheaper and more effective than inventing new mutation maths, and it is the single highest-leverage thing in this project.

---

## 2. Core concepts and vocabulary

| Term | Meaning in this project |
| --- | --- |
| **Gene** | One normalized parameter value in [0, 1] with metadata (curve, group, sensitivity, gates, growth-exemption). Genes have **no units** — the DSP interprets them. |
| **Genome / DNA** | The ordered vector of all 48 genes that fully defines a patch. |
| **Seed** | The currently selected genome plus a 64-bit `seedId`. Branch identity is derived from that id. |
| **Branch** | A derived genome: a deterministic mutation of the seed, parameterized by branch index and length. One branch per semitone of one octave. |
| **Length (growth)** | How far a branch has grown from the seed, in [0, 1]. 0 = identical to seed. Growing a branch **re-derives its gene values**; the reference exposes both the seed's value and the *grown* value of every gene (`dna.grown.<gene>`). |
| **Growth-exempt** | Genes deliberately excluded from growth (`rvb_size`, `adj_*`) "because it would cause disruption of the audio". |
| **Wheel** | A performance macro. In v2 the host chooses **what it drives** (`Wheel Target`, 9 values, including growth and LFO amount). |
| **Rotation** | Rotates which branch is assigned to which key (0-360°). Automating it is explicitly encouraged by the reference's own manual. |
| **Clone / plant** | The reference's "plant this branch": set all twelve lengths to that branch's length and advance `seed += branchIndex`. |
| **Layer / bulb mode** | v2 has 12 `layers` with `enabled` flags and a 3-value `bulbMode`. Reading: per-branch enable + plant rendering mode. **[?]** |
| **Strand / group** | A subset of genes that mutate coherently. Not present in the reference; our invention [ours]. |
The reference's plant layer is a real parameter namespace (`coreSize, baseHue, hueVariation, minRootAlpha, maxRootAlpha, min/maxBranchLength, twig*, twigMaxGenerations, twistMask, plantZoom, sway*, decay*, seedImage, hoverSeedSize, seedOffset`), and the plant is *not* decorative: "the plant is a view over the genome", and the twelve branch lengths are part of the patch, so the organic metaphor is load-bearing in the data model, not a skin on the UI.

---

## 3. System architecture

```
   UI thread / message thread                 Audio thread
  ┌───────────────────────────┐        ┌──────────────────────────────────┐
  │ Seed editor, plant view   │        │ MIDI in -> note->branch lookup   │
  │ Genome store (authoritative)│ ---> │ Branch genome snapshot (immutable)│
  │ Branch generator (growth)  │ lock- │ Voice pool (N voices)             │
  │ Preset save/load          │ free   │   each voice: genome -> DSP params│
  └───────────────────────────┘ swap   │ Master FX (reverb/chorus + gate) │
                                       │ Output EQ -> Volume -> soft clip │
                                       └──────────────────────────────────┘
```

**Design rules**

- The **growth calculation is computed off the audio thread**. When the seed, wheel, branch lengths or rotation change, build 12 resolved branch genomes (plain arrays of floats) and hand them to the audio thread via a lock-free triple buffer or atomic pointer swap.
- A voice **captures its branch genome at note-on** so that dragging a branch does not click already-sounding notes. Optionally crossfade to the new genome over about 50 ms for held notes.
- Resolving genes to DSP values happens at voice start and per control block, with smoothing.
- Keep the genome as `std::array<float, N>` with a compile-time `N` and an enum of gene IDs, so growth code is data-driven by a `GeneInfo` table rather than 48 hand-written lines.

### 3.1 What the reference's engineering choices teach us

Everything in this subsection is visible from outside the plugin — how it behaves in a host, what its files contain — and is worth imitating.

- **The parameter store *is* the model, and it is normalized floats.** `getParameter` echoes `setParameter` bit-exactly; transfer curves and units exist only at display time. Genes are unitless [0,1] floats displayed as raw `0.5`-style numbers. → **Decision [ours]:** our canonical state (genome, patch, preset, host-facing parameter values) is normalized [0,1]. `GeneInfo.curve/min/max` is *DSP-side mapping data*, never part of what is stored or displayed, and the gene UI shows the raw float like the reference does. That is what makes a 48-parameter surface legible in a DAW.
- **Compatibility by name, gated by version.** Genes are parsed **by name**, not position — its patch files name every value; the patch format carries a format/model version (`Version/Implementation: 10` in v1, `format: 28, implementation: 29` in v2). The product version, the framework version and the patch-model version are three separate axes. → See §11.
- **One allocation, no allocation in the audio path [ours].** One arena for the whole instrument, allocated up front, with the host-facing structure inside it. That is the shape that keeps a plugin real-time safe (§10).
- **The audio path stays small [ours].** The reference is not a CPU-heavy instrument; it plays many voices on one core. A 16-voice GenoMosh at a low CPU budget is therefore a realistic target, and a single per-block render function is a proven shape.
- **Data, not code, at the top.** The interface text, the help text of *every gene*, the tutorial, the factory presets, the patch-naming word list and the global constants are presented as content, not baked-in behaviour; re-documenting or re-skinning needs no code change. → §12: our gene help text is table data too.
- **Automatable ≠ addressable.** In v1 the host sees 21 parameters; the 37 genes are reachable *only through growth*, so automating `ModWheel`/`Branch1..12` automatically automates filter cutoff, reverb, LFO and envelopes. In v2 the author changed their mind and appended all 48 genes as parameters 26-73. → §9: we do both, like v2.

### 3.2 Deviation register

Deliberate departures from the reference. Keep them, and keep them labelled, so nobody mistakes them for reproduced behaviour.

| # | Deviation | Why |
| --- | --- | --- |
| D-1 | Coherent group directions and per-gene `softMin/softMax` in growth (§5.2). | The reference documents no growth algorithm; how a length becomes gene values is its own business. This is our design, and it is what makes "cousin" sounds coherent. |
| D-2 | Automatic level compensation and silence re-rolls (§5.6). | The reference instead hands the user a **per-branch `volume`** field. We do both: compensate automatically *and* expose per-branch trim. |
| D-3 | Slow per-voice drift and per-voice variation as non-gene behaviour (§7). | The project's own brief (`Docs/Morphing synth.md`) asks for more movement than the reference shows. |
| D-4 | Optional ratio-snapping on `b_freq` (§5.5). | The reference quantises `a_freq` to octaves and leaves `b_freq` free; the ratio snapper is our extension, kept behind Atonality. |
| D-5 | Fixed note-off behaviour: stop looping, 180 ms release (§6.1.1). | Operator decision (Rev 2 alignment D5). The reference's envelope simply *is* the note length and is stopped by key-up. |
| D-6 | Sparse modulation matrix (§6.3). | The reference's routing is fixed per destination; the matrix is our extension. |
---

## 4. Parameter model

### 4.1 Macro / performance parameters (host-automatable, *not* mutated)

All curves below are what the reference documents for its own macros, and are adopted as-is [ours]: they are well-chosen and they cost us nothing. `v` is the normalized value.

| ID | Meaning | Transfer / default |
| --- | --- | --- |
| `ModWheel` | **Grows all twelve branches at once.** Its behaviour is exactly: `branch = lerp(branch, 1.0, ModWheel × WheelScale)` | linear %, default **0** |
| `WheelScale` | How much the wheel is allowed to grow them | linear %, default **1.0** |
| `Rotation` | Rotates the branch→key mapping | linear 0-360°, default 0.5 (180°) |
| `Tuning` | Pitch of the whole plant | linear **±12 semitones**; default 0.5 |
| `Atonality` | Biases the genome *as it is generated*: `converge(a_mod, atonality)`, same for `b_mod` | linear %, default 0.5 |
| `Effect` | **Two values: effect mix *and* panning.** "*adjusts the effect mix and panning amount… completely dry monophonic → wet voluminous*"; reducing it also reduces branch panning | bipolar %, centred at v=0.5; default 0.5 |
| `Release` | **Two values: release time *and* reverb gating %** ("*the lowest settings will gate the built-in reverb so that it will be quickly muted when you release the keys*") | **`50 ms × 400^v`** (50 ms … 20 s); default v=0.25 → 1.0 s |
| `VelSens` | Velocity → modulation-envelope depth (works with the `mod_vel` gene) | linear %, default 0.5 |
| `Volume` | Output level **into a saturating limiter**: "*raising the volume excessively will make it distort*" — automating it automates the distortion | **`gain = (2v)^log2(10)`** = exactly 20 dB per doubling; 0 dB at v=0.5, +20 dB at v=1, −inf at 0 |
| `Branch1…12` | The twelve **growth lengths**. In v2 each branch is stored as `{ id: seedId + i, length, volume }` | linear %, default 0 |
| `Voice Mode` | 2 values (poly / mono-legato), with `Glide Time` (s) | discrete, 2 steps |
| `Wheel Target` | **What the mod wheel drives** — 9 values, including growth and LFO amount | discrete, 9 steps |
| `Tempo Sync` | 7 values; v2 asks the host for tempo information, v1 never did | discrete, 7 steps |
| `Bulb Mode` | 3 values (`standard`, `layered`, …). Semantics **[?]** | discrete, 3 steps |
**GenoMosh scope [ours]:** implement `ModWheel`, `WheelScale`, `Rotation`, `Tuning`, `Atonality`, `Effect` (both halves), `Release` (both halves), `VelSens`, `Volume` (with the limiter), `Branch1..12` with per-branch volume, and `Voice Mode` + `Glide Time` + a minimal `Wheel Target` (growth / LFO amount / filter cutoff) + `Tempo Sync` for the envelope and LFO. Defer `Bulb Mode` and `layers` beyond a per-branch enable flag.

`TriggerA…H` are **not** note triggers: they are the reference's own action slots, bound through its private MIDI-learn layer to *Undo, Redo, Previous Patch, Next Patch, Plant Seed*. Implement CC-bound actions, not eight mystery inputs.

### 4.2 Genes (mutated; the genome)

48 genes, in the reference's own grouping and order (as it lists them in its interface and in the `genome:` block of its patch files). Default value for every gene is **0.5**, except `osc_mix = 0.7937`, `flt_type = 0.6667`, `flt_freq = 0.75`, `b_freq = 0.6875` — the neutral-at-0.5 convention of §5.7.1, straight from its factory state. In the tables below, **Sens** is our own suggested starting mutation sensitivity for that gene (0 = never mutates, 1 = mutates freely); it is a tuning starting point, not a statement about the reference.

**The five most dangerous Rev 2 guesses, to keep out of the code** (full correction table in `Docs/Rev3-ChangeLog.md` §2): `vol_atk`/`vol_dcy`/`mod_atk`/`mod_dcy` are *curvature* controls neutral at 0.5, not times; `env_loop` is a *loop period*, not an amount; `mod_sh` and `b_sh` are *sample-and-hold rates*, not shapes or sync; `a_freq` is a 7-step octave grid that *is* the patch pitch (and the filter cutoffs track it); `a_color` is the *noise colour*, not a timbre control.

#### Group 1 — Envelope clock (4) + volume (4) + modulation (4) + LFO (4) = 16

One shared clock drives both envelopes. **The envelope duration is a gene**, so growth changes articulation as much as spectrum — this is the most character-defining group in the instrument.

| Gene | Behaviour (as the reference describes it) | Our mapping [ours] | Sens |
| --- | --- | --- | --- |
| `env_time` | Total duration of the sound; ≥0.9 → infinite, ended only by key-up | `Exp` 0.05 s → 30 s; v ≥ 0.9 = ∞ | 0.45 |
| `env_loop` | Loop **period** of that envelope; a period longer than `env_time` means no repeats | `Exp` 0.05 s → 30 s; repeats iff period < duration | 0.35 |
| `env_tilt` | Attack/decay skew of the shared envelope: low = fast attack + long decay, high = slow attack + fast decay | `Bipolar` around 0.5 = symmetric | 0.40 |
| `env_kf` | Key follow of duration; above 0.5 shortens the envelope on high keys | `Bipolar`, 0.5 = none, ±2 octaves of time scaling | 0.25 |
| `vol_atk` | Attack **curvature**, 0.5 = linear | `Bipolar` → shape exponent `2^(2(v−0.5))` | 0.35 |
| `vol_dcy` | Decay **curvature**, 0.5 = linear | same | 0.35 |
| `vol_sus` | Sustain: below 0.5 = no sustain, 1.0 = decay inaudible | gated: `sustain = max(0, 2(v−0.5))` | 0.40 |
| `vol_fade` | A second fade after the attack, only active above 0.75 | gated: 0 below 0.75, then a droop toward sustain | 0.25 |
| `mod_atk`,`mod_dcy` | Curvature of the **modulation** envelope — same shape model, drives *parameters*, not level | as `vol_atk`/`vol_dcy` | 0.35 |
| `mod_sh` | **Sample-and-hold rate for the modulation envelope**; only very high values activate it; higher value = slower | off below 0.75, then rate 8 Hz → 0.1 Hz (inverted) | 0.30 |
| `mod_vel` | How much velocity scales modulation depth; below 0.1 = no velocity | gated: 0 below 0.1 | 0.30 |
| `lfo_rate` | LFO frequency | `Exp` 0.02 → 30 Hz; tempo-syncable [ours] | 0.60 |
| `lfo_amt` | Bipolar; 0.5 = none; above 0.5 = A and B modulated **together**, below = **in opposite directions** (v1: below 0.5 = off) | `Bipolar` around 0.5; negative → opposite phase on A and B | 0.50 |
| `lfo_bal` | 0.0 = vibrato only, 0.5 = both, 1.0 = tremolo only | equal-power pitch/tremolo split at one rate | 0.50 |
| `lfo_dly` | Delay before the LFO fades in; none below 0.5 | gated: 0 below 0.5, then 0 → 8 s | 0.30 |
#### Group 2 — Oscillator A (5) + FM/mix (4) + Oscillator B (6) = 15

| Gene | Behaviour (as the reference describes it) | Our mapping [ours] | Sens |
| --- | --- | --- | --- |
| `a_form` | Waveform morph sine → saw → square → pulse; **breakpoints: sine 0.0, saw 0.57, square 0.81, pulse 1.0** | Band-limited morph at those exact breakpoints; duty stays 50% **[?]** (no PWM gene exists) | 0.50 |
| `a_noise` | Noise mix into A; 1.0 = fully obscured by noise | `Squared` | 0.40 |
| `a_mod` | **Bipolar pitch modulation from the mod envelope**, 0.5 = none | `Bipolar` → up to ±12 st, ceiling set by Atonality; `kFlagPitch` | 0.50 |
| `a_color` | **Colour of the noise: brown (muddy) → white (sharp)** | One shared noise-spectrum shaper (pink→white); B uses the same source **[?]** | 0.40 |
| `a_freq` | Pitch of A, **quantised to 7 steps over −3…+3 octaves**; it is the patch pitch; B's pitch and the filter cutoffs link to it | `Stepped`, 7 steps in octaves, default 0.5 = unison | 0.30 |
| `fm_mod` | How the envelope changes the FM amount, 0.5 = no effect | `Bipolar` | 0.50 |
| `fm_amt` | FM amount, **A modulated by B**, bipolar, 0.5 = none | `Bipolar` → index 0…20, alias-guarded | 0.50 |
| `mix_mod` | Mod envelope → A/B mix amount (new in v2) | `Bipolar` | 0.40 |
| `osc_mix` | A/B mix, 0 = only A, 1 = only B, **non-linear with 50/50 at ≈0.79** | Equal-power crossfade warped so 0.79 = equal; default 0.7937 | 0.40 |
| `b_form` | Waveform morph for B | as `a_form` | 0.50 |
| `b_noise` | Noise mix into B | `Squared` | 0.40 |
| `b_mod` | Bipolar pitch modulation of B from the envelope, 0.5 = none | as `a_mod`; `kFlagPitch` | 0.50 |
| `sub_am` | Sub oscillator = **AM of B one octave lower**, mix amount | True AM (B/2 ring-biased at B÷2), not a sine sub | 0.30 |
| `b_freq` | Pitch of B relative to A; default 0.6875; not quantised | −24…+24 st with 0.5 = unison; optional ratio snapping (D-4) | 0.35 |
| `b_sh` | **S&H rate for B**; below 0.5 the rate is synced to B's frequency for consistent timbre across pitches | v<0.5: rate = B's frequency ÷ (1…8); v≥0.5: free 0.5…16 Hz | 0.35 |
#### Group 3 — Filter (6) + saturation (1) + reverb (6) + output adjust (4) = 17

| Gene | Behaviour (as the reference describes it) | Our mapping [ours] | Sens | flags |
| --- | --- | --- | --- | --- |
| `flt_type` | **0.0 bandpass · 0.33 dual serial LP · 0.66 dual parallel LP · 1.0 notch** | `Stepped` over those four, nearest-step; crossfade topology changes | 0.30 | structural |
| `flt_q` | Resonance | `Squared`, capped as a function of type/freq/sep (§5.6) | 0.40 | |
| `flt_mod` | Bipolar cutoff modulation, 0.5 = none | `Bipolar` | 0.50 | |
| `flt_sep` | Separation of the two filter cutoffs, **0.5 = no separation** | `Bipolar` in octaves, 0.5 = 0 oct, ±3 oct | 0.40 | |
| `flt_freq` | Cutoff **relative to A's frequency**; default 0.75 | `cutoff = fA × 2^lerp(−2, +8, v)` — tracking, so branches keep their relative timbre at every pitch | 0.50 | linked |
| `flt_kf` | Key follow, **soft-railed**: <0.25 none, >0.75 exact | `smoothstep((v−0.25)/0.5)` × 1 octave/key | 0.30 | |
| `saturate` | **Per-voice** warm distortion. `Rev2-Answers` A2 describes it as a *master* post-filter stage and `adj_clip` as the per-voice one **[?]** | Per-voice, after the filter, 2× oversampled, DC blocked. Off until ~0.5, then drive ↑ | 0.40 | |
| `rvb_mix` | Initial dry/wet of the built-in "**chorusing reverb**"; "*the mix can be further adjusted with the Effect slider*" | Linear, scaled by `Effect` | 0.30 | |
| `rvb_atk` | Reverb-amount **envelope**, active above 0.66 | gated: 0 below 0.66, then bloom time 0…4 s | 0.30 | |
| `rvb_len` | Decay time | `Exp` 0.2 → 12 s RT60 | 0.40 | |
| `rvb_damp` | High-frequency damping | Linear per-line one-pole | 0.40 | |
| `rvb_chor` | Chorus/unison amount added to the tail | Linear, scales depth+rate together | 0.40 | |
| `rvb_size` | Perceived room size — **does not change when branches grow** | Linear delay-line scale; **growth-exempt** | 0.0 | `kFlagNever` |
| `adj_bass` | Low shelf **±24 dB at 100 Hz** | `Bipolar`, growth-exempt | 0.0 | `kFlagNever` |
| `adj_treb` | High shelf **±24 dB at 6 kHz** | `Bipolar`, growth-exempt | 0.0 | `kFlagNever` |
| `adj_pan` | **Branch panning amount** — how far branches are spread across the field; reduced by lowering `Effect` | `Bipolar` spread, growth-exempt | 0.0 | `kFlagNever` |
| `adj_clip` | **Master soft clipper after the master volume** | Linear drive, growth-exempt | 0.0 | `kFlagNever` |
**Migration from the current 43-gene table.** The code (`Source/Genetic/GeneTable.h`) is still Rev 1's set. Rename `fx_*→rvb_*`, `balance→osc_mix`, `sat_drive→saturate`; delete `b_color`, `det_spread`, `drift_amt`, `vol_rel` (per-voice life stays non-gene, per-voice release becomes a fixed behaviour per D-5); add `env_kf`, `vol_fade`, `lfo_dly`, `mix_mod`, `flt_type`, `rvb_atk`, `adj_bass`, `adj_treb`, `adj_pan`, `adj_clip`. `Docs/Rev3-Alignment-Plan.md` is the plan of record for executing that migration (its milestone M0); `Docs/Rev2-Alignment-Plan.md` is superseded, since its stage list assumed Rev 2's gene *semantics*. Bump `kNumGenes`, and treat the old preset format as unreadable (a `mutationVersion`/model bump, §11).

### 4.3 Gene metadata table (implement as data)

```cpp
struct GeneInfo {
    const char* id;          // e.g. "flt_freq" — also the preset key (name-keyed format, §11)
    const char* label;       // UI name
    const char* helpText;    // one sentence, shown on hover. The reference documents one per gene.
    Curve       curve;       // Linear, Exp, Squared, Bipolar, Stepped
    float       minValue, maxValue;   // DSP-side mapping only; never stored, never displayed
    float       neutral;     // the "nothing happens" value: 0.5 for every bipolar gene (§5.7.1)
    float       gateLow, gateHigh;    // dead zone / activation threshold (§5.7.3); 0,1 = none
    Group       group;       // Env, Vol, Mod, Lfo, OscA, Mix, OscB, Filter, Sat, Revb, Out
    uint32_t    steps;       // for Curve::Stepped: a_freq=7, flt_type=4
    float       sensitivity; // 0..1 growth scale
    float       softMin, softMax;     // preferred musical region for random seeds and growth
    uint32_t    flags;       // kFlagNever (growth-exempt), kFlagPitch, kFlagStructural, kFlagLinked
    uint32_t    colour;      // group colour for the UI, one per gene group
};
```

`neutral`, `gateLow/gateHigh`, `steps` and `kFlagNever` are new in Rev 3 because the reference's interface shows them in use everywhere (§5.7). The `helpText` field exists because the reference documents every gene in its own interface (§12).

---

## 5. The genetic engine

**Honest scope note.** The reference documents no growth algorithm. Nothing in its interface explains how a branch length turns into gene values, and its own vocabulary avoids `mutate`, `crossover`, `evolve` and `pollinate` — it talks only about growing branches. What its interface *does* pin down is the **interface**: branch id = `seedId + branchIndex`, per-gene grown values exist and differ from the seed's, five genes are exempt, and the wheel formula is a lerp. So everything below remains our design, with those constraints as gates.

### 5.1 Requirements

1. **Deterministic**: the same `seedId`, branch index and length always give the same genome. No fresh randomness at play time.
2. **Continuous**: a small increase in length gives a small change in sound, so growing a branch is audible as a gradual morph.
3. **Seed-anchored**: length 0 returns the seed exactly.
4. **Diverse**: different branches must go in noticeably different directions.
5. **Safe**: results pass guard rails (§5.6).
6. **Fast**: all 12 branches are cheap (about 12 × N operations); computed off-thread on any edit.

### 5.2 Branch growth

Each branch has a fixed random **direction vector** derived from a hash of `(seedId, branchIndex)` — which is how the reference identifies a branch, so this is not a guess.

```cpp
Genome growBranch(const Genome& seed, uint64_t seedId, int branch,
                  float length /*0..1*/, const GrowthParams& gp)
{
    Rng rng(hash64(seedId, branch));

    // 1. Per-group coherent directions + per-gene individual directions  [ours, D-1]
    float groupDir[NumGroups];
    for (auto& g : groupDir) g = rng.gaussianClamped(1.0f);

    Genome out;
    for (int g = 0; g < N; ++g) {
        const GeneInfo& gi = kGenes[g];
        if (gi.flags & kFlagNever) { out[g] = seed[g]; continue; }   // §5.7.6, growth-exempt: rvb_size/adj_*

        float indiv = rng.gaussianClamped(1.0f);
        float dir   = lerp(indiv, groupDir[gi.group], gp.coherence);      // ~0.35..0.6

        // 2. Non-linear length response: gentle near the seed, wilder far out
        float d = std::pow(length, gp.lengthExponent);                    // ~1.5..2.2

        // 3. Displacement, biased toward the gene's neutral value
        float delta = dir * gi.sensitivity * gp.globalScale * d;
        float x = seed[g] + delta + gp.pullToNeutral * (gi.neutral - seed[g]) * d;

        out[g] = softCompress01(x, gi.softMin, gi.softMax);   // monotonic, exact at d=0
    }

    applyStructuralTraits(out, rng, length, gp);   // §5.4, thresholds at the reference's own breakpoints
    applyCouplings(out, gp);                       // §5.7.8 (Atonality's converge is here)
    return out;
}
```

**Why this shape**

- *Group coherence* makes branches sound like cohesive timbral variations instead of 48 independent random walks, which tend to sound like noise.
- *Length exponent* gives a wide zone of close family resemblance and a fast-growing wild zone.
- *Monotonic soft compression* (already implemented) instead of reflection: reflection is continuous but not monotonic, so growing visibly bounces.
- *Pull toward `neutral`* is new in Rev 3: with the reference's neutral-at-0.5 convention (§5.7.1), a random walk that drifts toward the middle of every range drifts toward "less of everything", which is much safer than drifting toward the ends.
- *Per-gene sensitivity* is where taste lives. Tune it by ear, constantly.

### 5.3 Wheel, rotation and branch lengths

Adopt the reference's wheel formula:

```cpp
// ModWheel grows all twelve at once; it interpolates toward fully grown, it does not scale.
effLength[i] = lerp(branchLength[i], 1.0f, clamp01(modWheel * wheelScale));
```

This is better than Rev 2's multiplication in three ways: it is what the reference actually does; `ModWheel=0` means "exactly as the patch says", so the code's current workaround of defaulting the wheel open becomes unnecessary; and it can only *increase* growth, which is what "growing" means.

- **`Wheel Target` [ours, following v2]**: let the wheel drive growth (default), LFO amount, or filter cutoff. The reference offers nine targets.
- **Note mapping**: `branchIndex = (midiNote − rootNote + rotation·12) mod 12`. Different octaves use the same branch. The **seed** is what plays at length 0.
- **Per-branch `volume` and enable**: v2 stores a trim per branch; implement both as patch data (D-2).
- Re-resolve genomes when any input changes; smooth on the audio side, resolve off-thread at control rate (100-200 Hz max).

### 5.4 Structural changes (rare, length-gated)

Continuous nudging is not enough for the "organic" feel. Add rare discrete changes whose probability rises steeply with length (`p = length^3 · rate`), expressed as **threshold traits** so they are deterministic and monotonic (a trait flips once as a branch grows and never flips back — already implemented):

- step `flt_type` between the four topologies (a topology change, so crossfade it, never slew it),
- switch `sub_am`'s mode, or enable/disable noise,
- flip a modulation destination's sign,
- swap which modulator (LFO / mod env / S&H) drives a destination (§6.3),
- move a `b_sh` or `mod_sh` rate into or out of its synced mode.

The reference's own breakpoints are the best available guide for where these boundaries are musically (saw at 0.57, square at 0.81, `vol_fade` above 0.75, `rvb_atk` above 0.66, `lfo_dly` above 0.5, `flt_kf` rails at 0.25/0.75). Use them as the constants in our own thresholds.

Each must be **crossfaded** in the voice (no clicks).

### 5.5 Atonality, pitch genes and ratio snapping

Two distinct things, now separated:

1. **What Atonality does**:
   ```
   converge(x, at)  =  x += (0.5 - x) * (1 - at)      // applied to a_mod and b_mod
   ```
   at genome-generation time, not at DSP time. `Atonality = 0` pulls the pitch-modulation genes to their neutral value (no vibrato from the envelope) → musical, playable; `Atonality = 1` keeps whatever the genome says → atonal, sound-effect-like. This is exactly the `applyCouplings` hook we already have, so implement it there. It also matches the operator's Rev-2 answer D11 ("a blend affecting the amount of vibrato"), and is at odds with D7 ("atonality should not affect pitch movement") — **flagged as §15 Q-6**.
2. **What the pitch genes do**: `a_freq` is the patch pitch and is **quantised to whole octaves** (seven steps, `MIN_PITCH=−3 … MAX_PITCH=+3`). Fine pitch is not a gene: it is the `Tuning` macro (±12 semitones) plus a non-gene `pitchAdjust` patch field.
3. **Our extension [ours, D-4]**: leave `a_freq` quantised (that is what keeps a plant tuneful across the keyboard) and put the ratio-snapping stage on **`b_freq`** instead: compute a candidate offset in semitones, snap toward a table of musical ratios (octaves, fifths, fourths, thirds, harmonic-series entries), and let Atonality blend snapped→free. This keeps Rev 2's idea, which is good, and moves it to the gene where the reference actually leaves freedom.

### 5.6 Guard rails

Implement all of these in `applyCouplings` and in the voice:

- **Level compensation**: estimate loudness from gene values (FM depth, resonance, mix, noise, saturation, and the average level of the amp envelope) and compensate so branches sit within about 6 dB of the seed. Note the reference does *not* fully solve this itself — it hands the user a per-branch volume instead, so keep both (D-2).
- **Resonance safety**: cap `flt_q` as a function of `flt_type`, `flt_freq` and `flt_sep`. No self-oscillation unless deliberately allowed.
- **Silence rejection**: if a branch's `env_time`/`vol_sus`/`vol_fade`/`osc_mix`/noise imply near-silence, re-roll with the next seed in the hash chain, bounded retries (already implemented). Note that with `vol_sus` gated at 0.5 and `env_time` being the note length, this check is *much* more likely to fire than Rev 2 assumed — it is load-bearing.
- **Runaway protection**: soft limiter on the voice and master bus; DC blocker after FM, sub-AM and S&H.
- **Range curation**: `softMin`/`softMax` keep random walks in the musical middle.
- **Denormal protection** in feedback paths (§10).

### 5.7 Curation mechanisms the reference uses — copy these

This section is the practical payload of Revision 3. Each mechanism is cheap to implement and each one raises the fraction of random genomes that are usable.

1. **Neutral at 0.5.** Every bipolar gene is defined so that 0.5 = "nothing happens" (`a_mod`, `b_mod`, `fm_amt`, `fm_mod`, `flt_mod`, `mix_mod`, `flt_sep`, `lfo_amt`, envelope curvatures), and every gene's factory default is 0.5. A random genome is therefore statistically centred on "mild". *Do the same; put `neutral` in the metadata.*
2. **Linking.** One gene serves several stages: `a_freq` sets A's pitch *and* links B's pitch and both filter cutoffs; `Effect` scales `rvb_mix` and panning; `VelSens` × `mod_vel`. A tracking filter (`flt_freq` relative to A) is why branches keep their timbre at every pitch. *Model links in the table (`kFlagLinked`) and resolve them once in `VoiceParams`, not ad hoc in the DSP.*
3. **Activation gates and dead zones.** A large share of genes are *off* over part of their range: `vol_sus` (<0.5 = none), `vol_fade` (>0.75 only), `rvb_atk` (>0.66 only), `lfo_dly` (>0.5 only), `mod_vel` (<0.1 = none), `mod_sh` (only very high values), `env_loop` (no repeats if period > duration), `b_sh` (synced below 0.5). This is the cheapest possible way to make "mostly nothing" the most likely outcome of a random value. *Model as `gateLow/gateHigh`.*
4. **Soft rails instead of hard clamps.** `flt_kf`: nothing below 0.25, exact above 0.75, smooth in between. Prefer this for any "amount of tracking" control.
5. **Deliberate quantisation.** `a_freq` on 7 steps, `flt_type` on 4, `b_sh`'s rate synced to B's frequency "for a consistent timbre across pitches". Continuity is not a virtue where a continuous value would sound unstable.
6. **Growth-exemption.** v1 exempted `fx_size`, v2 exempts `{rvb_size, adj_*}`, and the reason is given in its own help text: "*this gene does not change when growing branches because it would cause disruption of the audio*". Room size and the master EQ/clip/pan must not move under growth. *Model as `kFlagNever` (the flag already exists in the code).*
7. **Non-linear, musical curves.** `osc_mix` 50/50 at 0.79, `Volume` exactly 20 dB per doubling, `Release = 50 ms × 400^v`. Adopt these verbatim for the macros.
8. **Couplings as one-line generation-time edits.** `converge()` (§5.5). Keep `applyCouplings` as pure, unit-tested, genome-space code — the reference applies its bias when the genome is *created*, never per sample.
9. **Identity as data.** The reference encodes seeds as short base-32 strings over a 17-character alphabet (`23456789abcdefghjkmnopqrstuvwxyz`, no ambiguous glyphs) and generates patch names from a large word list. Cheap, on-theme, and it makes seeds shareable by mouth. *Adopt both (§11).*

### 5.8 Optional: "breeding"

Growing a branch = the user drags it. Choosing a branch = **planting** it. The reference's own clone operation: set all twelve lengths to that branch's length and `seed += branchIndex` — which is exactly the deterministic way to make a new seed id out of an existing one. Use that. Optional extensions are crossover between two genomes (per-group pick or blend) and "mutate this seed by X%".

---

## 6. Modulation system

### 6.1 Sources

- **The shared envelope** (§6.1.1): one clock drives the amp and the modulation envelope.
- **LFO** (`lfo_*`): one rate, bipolar amount (in-phase or opposite-phase on A/B), pitch/tremolo balance, onset delay, optional tempo sync, optional per-voice phase offset.
- **Sample-and-hold** at two places: mod-envelope output (`mod_sh`) and oscillator B (`b_sh`), both with rate controls, `b_sh` note-synced below 0.5. This is a *third* modulator, which Rev 2 did not have.
- **Velocity** (`VelSens` × `mod_vel`), **key follow** (`flt_kf`, `env_kf`), **mod wheel** (per `Wheel Target`), **pitch wheel** (`PitchWheelRange`).
- **Noise / random-walk drift**: a slow smoothed random source per voice [ours, D-3]. Not a gene in the reference; central to this project's brief.
- **Branch position → pan**: the reference pans by branch position, scaled by `adj_pan` and reduced by `Effect`. Free "movement" across the keyboard; implement it.

### 6.1.1 The shared envelope (revised)

**What the reference states.** There is one shared envelope shape feeding *both* volume and modulation, and its time base is the note itself: `env_time` is the **total duration of the sound**, and at high values it is "infinite, stopped only by key-up". `env_loop` is a **loop period**, and repeats disappear when the period is longer than the duration. The curvature genes (`vol_atk`/`vol_dcy`, `mod_atk`/`mod_dcy`) are shape exponents with 0.5 = linear, not times. Rev 2's "looping envelope engine" was structurally right and wrong about every knob.

**Design [ours unless marked].**

- Cycle: `T = envTime · keyFollowFactor(env_kf, note)`, where `env_kf` > 0.5 shortens the envelope on high keys. `env_time ≥ 0.9` ⇒ `T = ∞`.
- `env_tilt` skews attack/decay of that shape: low = fast attack + long decay, high = slow attack + fast decay.
- Repeats: if `envLoopPeriod < T`, re-trigger the shape every period while the note is held; otherwise it plays once.
- Curvature: attack and decay are power curves with exponent `2^(2(v−0.5))`, so 0.5 is exactly linear.
- `vol_sus`: `max(0, 2(v−0.5))` — the floor the decay falls toward, with a genuine "no sustain" region.
- `vol_fade`: a second fade *after* the attack toward `vol_sus`, active only above 0.75. (Rev 2's "peaks step down across repeats" reading is dropped.)
- The mod envelope uses the same clock and tilt with its own curvature, scaled by `VelSens × mod_vel`, optionally re-quantised by an S&H at rate `mod_sh`.
- Note-off [ours, D-5]: stop looping, then a fixed 180 ms release on the amp envelope. Keep it a named constant; a user-settable release is a possible future parameter. The reference gets away without one because the envelope *is* the note length and key-up simply ends it.
- The `Release` **macro** is a different thing: a global release time plus a reverb **gate** (§8). Do not conflate them.

```cpp
// Per voice, per control block. Envelope names are genes; the behaviour here follows the
// reference unless marked [ours].
float T = envTime * (envKf > 0.5f ? keyShorten(note) : 1.0f);   // env_kf above 0.5 shortens
if (envTime >= 0.9f) T = kInfinite;                              // stopped only by key-up
float shapeAtk = std::exp2 (2.0f * (volAtk - 0.5f));             // 0.5 = linear
float shapeDcy = std::exp2 (2.0f * (volDcy - 0.5f));
float sus      = std::max (0.0f, 2.0f * (volSus - 0.5f));       // below 0.5 = no sustain
float fade     = volFade > 0.75f ? (volFade - 0.75f) * 4.0f : 0.0f;  // active above 0.75

phase += blockSeconds / T;
if (envLoopPeriod < T && phase >= envLoopPeriod && noteHeld) { phase -= envLoopPeriod; ++cycle; }
float p = tiltWarp(phase / T, envTilt);                          // attack/decay skew
float volEnv = peakSag(p, shapeAtk, shapeDcy, sus, fade) * gateAfterNoteOff();
float modEnv = shaped (p, modAtk, modDcy) * velScale;            // drives parameters, not level
if (modShActive) modEnv = sampleAndHold(modEnv, modShRate);      // very high gene values only
```

### 6.2 Destinations

Modulation envelope → oscillator pitch (`a_mod`, `b_mod`), FM amount (`fm_mod`), oscillator mix (`mix_mod`), filter cutoff (`flt_mod`). LFO → **pitch (vibrato) and volume (tremolo)** at one rate, split by `lfo_bal`, with the polarity trick of `lfo_amt` (§4.2) — consistent with the reference's own screenshot showing vibrato and tremolo at the same rate with different amounts. Sample-and-hold → mod envelope output and oscillator B. Filter separation and effect parameters are additional targets [ours].

### 6.3 The organic extension: sparse modulation matrix as genes [ours, D-6]

The reference's routing is fixed per destination. To get *evolving* modulation, treat those amounts as a matrix of weights where growth can (1) nudge weights continuously, (2) rarely switch a destination's source between mod-env / LFO / S&H (structural, crossfaded), (3) rarely add or remove a routing. Cap active routings at 6-10 so patches stay legible. Keep amounts bipolar and squared so small values stay subtle.

### 6.4 Smoothing

All modulation targets are smoothed (5-20 ms, one-pole or linear ramp) except envelope-driven targets with fast attacks. Sample-accurate envelopes; block-rate or 16-32 sample sub-block control updates for the rest. S&H outputs must not be smoothed through their step — the step is the sound.

---

## 7. Voice architecture

Placement follows the reference's own grouping and help text; the two contested points are marked.

```
 note -> pitch -> [Osc A: form/stepped octave] --+--(FM: A modulated by B)--> A
                  [Osc B: form, S&H b_sh, sub = AM at -1 oct] --------------- B
                  [Noise: colour brown->white] -> a_noise / b_noise into A and B
   A,B -> osc_mix (50/50 at 0.79) + mix_mod
       -> filter network: flt_type { bp | dual serial LP | dual parallel LP | notch }
                            cutoff locked to A's pitch + flt_sep + flt_q + flt_kf(soft-railed)
       -> [saturate: per-voice, 2x oversampled]        <-- placement [?], see §15 Q-5
       -> amp envelope (shared clock, §6.1.1)
       -> per-branch pan (adj_pan spread) and per-branch volume
       -> master: adj_bass/adj_treb shelves -> Volume -> saturating limiter + adj_clip
       -> reverb/chorus (rvb_*, gated by the Release macro)
```

- `a_freq` links B's pitch and the filter cutoffs: resolve that once in `VoiceParams`, so every branch tracks the keyboard.
- **Oscillators**: band-limited (PolyBLEP/BLAMP or mip-mapped wavetable). Morph through the observed breakpoints (saw 0.57, square 0.81) without zipper noise. FM (A modulated by B) needs 2-4× oversampling or band-limiting; aliasing is the usual reason digital synths sound cheap.
- **Sub**: true amplitude modulation of B one octave down, not a sine sub.
- **Filter**: default topology is *two parallel low-pass filters* whose cutoffs separate with `flt_sep` — Rev 2's "two cascaded stages" is not what the reference defaults to, and serial vs parallel is exactly what `flt_type` selects. Zero-delay-feedback state-variable sections; crossfade topology changes.
- **Saturation**: two stages in the reference, one per-voice (`saturate`) and one master (`adj_clip`, after master volume). v1 had only an unnamed saturating limiter behind `Volume`.
- **Voice-level life [ours, D-3]**: per-voice offsets (tiny detune, phase start, drift phase, mod-time offsets) and slow drift on pitch and cutoff, seeded per note-on. Not genes.
- **Polyphony**: 16 voices (operator decision, Rev 2 answer D16), release-aware stealing with a real fade on stolen voices, plus a mono/legato mode with glide (v2's `Voice Mode`/`Glide Time`).

---

## 8. Effects (chorusing reverb) and output stage

Genes `rvb_mix, rvb_atk, rvb_len, rvb_damp, rvb_chor, rvb_size` and the macro `Effect`, which carries **mix and pan**. The reference calls the tail "*a chorusing reverb*" in its own help text, with chorus as one gene on the tail rather than a separate stage — so put the modulation **inside** the reverb.

**Recommended architecture [ours]**

- **Reverb**: 8-line FDN with a Hadamard/Householder matrix, input allpass diffusion, per-line one-pole damping (`rvb_damp`), RT60-based feedback from `rvb_len`, mutually prime delay lengths scaled by `rvb_size`, slight length modulation to kill metallic ringing.
- **Chorus/unison**: 2-4 modulated taps inside or across the tail, stereo-decorrelated, slow incommensurate LFOs; `rvb_chor` scales depth and rate together.
- **`rvb_atk`**: a reverb-amount envelope, gated to values above 0.66 — the wet send blooms in over time instead of starting instantly.
- **Reverb gating from the `Release` macro**: the second value of `Release` mutes the tail quickly on key-up for short release times. Implement as a gate on the wet bus driven by `1 − releaseGateAmount`, separate from the amp release. This is a cheap and very "organic" articulator.
- **Mix**: equal-power wet/dry from `rvb_mix`, scaled by `Effect`; `Effect`'s second half scales the branch pan spread, and lowering it reduces panning, as the reference's manual says.
- **Growth and `rvb_size`**: the reference exempts size from growth because changing delay lengths mid-tail is disruptive. Follow that (`kFlagNever`) rather than crossfading two reverbs; keep crossfade machinery only for direct user edits of size.
- **Output stage** (`adj_*`, all growth-exempt): low shelf ±24 dB at 100 Hz, high shelf ±24 dB at 6 kHz, branch-pan spread, then a master soft clipper **after** the master volume. The order of the shelves relative to the reverb is not documented — only the clipper's position is [?].

### 8.1 Level and limiter

`Volume` is `gain = (2v)^log2(10)` — 20 dB per doubling, −inf at 0, +20 dB at 1 — feeding a saturating limiter in v1 and `adj_clip` in v2. Implement the curve verbatim, and put the limiter's drive off the same normalized value, so "too loud" becomes "distorted" the way the reference's tutorial promises.

---

## 9. MIDI and host integration

The reference's MIDI settings, as its own setup page lists them:

| Setting | Notes |
| --- | --- |
| `MIDIReceiveChannel` | "any" or 1-16 |
| `MIDITransmitChannel` | "none" or 1-16 |
| `PitchWheelRange` | semitones |
| `VelocityCurve` | `soft` / `standard` / `hard` — three monotonic curves |
| `SupportProgramChanges`, `SelectWithMIDI` | program change → presets; MIDI note selects the active branch |
| MIDI learn | the reference's **own** MIDI-learn layer: fourteen CC slots (tuning, atonality, effect, release, wheel scale, velocity sense, volume, mod wheel, rotation) plus five action triggers |
Rules for GenoMosh:

- Expose every macro as an automatable `AudioProcessorValueTreeState` parameter, **and** expose the 48 genes as parameters too, the way v2 does (parameters 26-73, raw 0..1 display). In v2 that is what a DAW shows, and it is what lets the user grow a branch by hand. Keep them in one unit/group named "Genome" so the list stays navigable.
- Answer **`IMidiMapping` and `IMidiLearn`** in VST3: v2 answers both, and Sonic Charge's own position is that MIDI CC should map to exposed/automatable parameters. That is exactly why a host that refuses to do that breaks their MIDI-learn; we should not repeat the failure.
- Branch lengths are patch data, and automatable (operator decision D9 said automatable while dragging; the reference has always made them automatable).
- Handle sustain, pitch bend (`PitchWheelRange`), mono/legato + glide, and tempo sync. Note that v2 asks the host for tempo and v1 never did — if we add `Tempo Sync` we must handle a missing tempo gracefully.
- Persist `seedId`, genome, branch lengths + trims + enables, wheel state and target, rotation, atonality, FX macro, and the MIDI config.

---

## 10. Real-time and engineering constraints

- **Language/framework:** modern C++ (17+), JUCE.
- **Audio thread:** no heap allocation, no locks, no file/IO, no `std::function` construction, no exceptions. Preallocate everything in `prepareToPlay`, in one arena for the whole instrument [ours, §3.1].
- **Genome hand-off:** immutable snapshot swapped via a lock-free structure (atomic pointer with a small ring of preallocated snapshots). A per-branch cache keyed by (snapshot revision, quantised distance) is the accepted hybrid (Rev 2 answer A1).
- **Denormals:** `juce::ScopedNoDenormals` plus explicit tiny DC/noise in long feedback paths.
- **Sample-rate independence:** every time constant and coefficient derives from the actual sample rate. Test at 44.1, 48, 96 kHz.
- **Determinism:** platform-independent PRNG (splitmix64/xoshiro + own gaussian), never `std::normal_distribution`. Deterministic rendering is what makes the offline golden-file regression of §13 possible.
- **Precision:** the reference's host interface offers a double-precision processing path. Float is fine for us where filters are stable; use double for long feedback-delay lines and phase accumulators.
- **Parameter smoothing** everywhere an audible zipper could occur; topology changes are crossfaded, never slewed.
- **Unit-testable core:** genome, growth and mapping code must be pure and independent of JUCE audio classes (already true and load-bearing).
- **Budget:** the reference is not a CPU-heavy instrument — it plays many voices on one core — so a 16-voice GenoMosh is not expected to be a CPU problem. Measure before optimizing; a synth engine of this kind concentrates its cost in a handful of loops, which is a warning against speculative optimization.

---

## 11. State and presets

- **Name-keyed, version-gated text format**, like the reference's: parse genes by name so the order can change, and check a version before loading. v2's layout is a good model: a `branches` array of `{id, length, volume}`, a flat `control` object, a `genome` object of plain 0..1 floats, per-branch `layers`, plus `seedId`, `pitchAdjust`, `modified`, `name`.
- **Three separate version axes**: plugin/product version, engine-model version (what the parameters *mean*), and growth-algorithm version. The reference keeps format and implementation versions apart (`format: 28` vs `implementation: 29` in its own files) — do the same: `formatVersion` (file shape) and `modelVersion` (gene set and semantics), and keep `mutationVersion` for the growth maths. Changing sensitivities or hash inputs rewrites every saved branch, so an old patch must be re-grown, not re-interpreted.
- **Compatibility handling**: a `legacyMode` flag like v2's, plus an explicit migration table for the Rev 1 → Rev 3 gene set (old presets from the 43-gene table are unreadable; write a converter that maps the carried-over genes and drops the rest).
- **Init and randomize**: the reference's randomise behaves as a gaussian centred on each gene's default, reflected at the rails (spread of about a quarter of the range). Adopt that as the default randomizer (a gaussian around each gene's neutral, reflected at the rails), with `softMin/softMax` as an extra per-gene clamp on top.
- **Seed codes and names [ours, following the reference's practice]**: encode `seedId` as a short base-32 string over `23456789abcdefghjkmnopqrstuvwxyz` (no ambiguous glyphs) so seeds can be typed and shared, and generate patch names from a shipped word list.

---

## 12. Optional UI: the plant view

The reference's plant vocabulary is real parameter data (`coreSize, baseHue, hueVariation, root/branch/twig limits, twistMask, plantZoom, sway*, decay*, seedImage, hoverSeedSize, seedOffset`), and the plant sways as if it were physical. Minimal approach [ours]:

- Central seed; 12 radial branches at 30° intervals offset by `rotation`; on-screen length = effective length; hue derived from the genome; twigs as deterministic sub-branches so the plant is a fingerprint of the genome; gentle idle sway; fade for unused branches.
- Interactions: drag a branch to grow it, drag a rotation handle, a mod-wheel arc, click the seed to plant, hover shows note names.

**What Rev 3 adds, because the reference does it and it is cheap:**

- **Gene help text as data**: one sentence per gene, in the gene table, shown on hover. The reference documents its genes this way, and it is the fastest way to make a 48-gene instrument learnable.
- **Gene colours per group**: the reference colours every gene and keeps the colours consistent with its page groups. Group colours make a 48-gene page scannable.
- **Show grown vs base value**: the reference's own view distinguishes the seed's value of a gene from the *grown* value of that gene in the selected branch (`dna.grown.<gene>`). A knob that shows "what the seed says" and "what this branch grew to" makes growth visible, which is the whole point of the instrument.
- **Live response graphs per group**: every gene page in v2 draws the curves its own genes produce (oscillator, filter, EQ, LFO, reverb, saturation, modulation and volume plots). Draw the envelope/LFO/filter curves, not just knobs.

Build this **after** the audio engine and a plain debug UI (48 sliders plus wheel, rotation and per-branch trims) work well.

---

## 13. Testing and tuning workflow

Tuning the gene table is the real work, so the tooling lands early.

**Adopt this golden-file regression protocol** [ours]. A seeded, offline-renderable instrument should be tested exactly this way:

```
perform('createTest',  { seed: 1, time: 300, sr: 22050, density: 10.0, file: "...Golden22050.wav" })
   ... 44100, 48000 (300 s), 96000 (150 s), 192000 (60 s)
perform('performTest', { ..., tolerance: 0.001 })
massiveAudioTest : 10 000 rounds, random seed, random sr < 100 kHz, density = sqr(random(10)) + 1
generateRandomNoises: 100 files, density = (i/10)^2 + 1
```

That is a **seeded note generator at a chosen note density, rendered offline at a chosen sample rate, written to WAV, and byte-compared with a tolerance**. Our harness exposes exactly this vocabulary (`seed`, `time`, `sr`, `density`, `file`, `tolerance`) and nothing more — it is a well-chosen shape and it makes cross-version regression cheap.

**Offline render harness (command-line)** — input: seed id or genome, branch index, length, note, velocity; output: WAV. Batch mode: all 12 branches at lengths 0.0, 0.25, 0.5, 0.75, 1.0. Plus the massive sweep above as a soak test.

**Automated checks per render**

- Peak and RMS level, loudness relative to the seed (target within ~6 dB).
- Silence detection, NaN/Inf detection, DC offset.
- Spectral centroid and flatness, to catch all-noise or all-dull branches.
- Aliasing estimate (energy above the expected harmonic series at high notes).
- Reproducibility: same inputs ⇒ bit-identical output.
- **Envelope-articulation check** (new): measure the rendered amplitude envelope per branch and assert it moves in the direction `env_time`/`vol_sus`/`vol_fade`/`env_loop` imply. With duration-as-a-gene, growth is as much about articulation as spectrum, and a regression here catches whole classes of bad genomes.

**Caveat**: audio captured from the reference in an unregistered host with no tempo information is not representative audio. Anything we compare against must be recorded in a registered host with time info supplied.

**Human evaluation loop** — generate many random seeds; listen to branches at several lengths; log which genes cause bad results and adjust `softMin/softMax`, `gateLow/gateHigh`, sensitivity and couplings; compare character (not timbre) to reference recordings of the target aesthetic.

---

## 14. Implementation phases (with acceptance criteria)

| Phase | Deliverable | Done when |
| --- | --- | --- |
| 0 | **Gene table re-semantified** to the 48 documented genes with `neutral`, `gateLow/High`, `steps`, `kFlagNever`, `helpText`; rename/add/delete per §4.2; old presets rejected or migrated | Table compiles with 48 entries; every gene's neutral and gate documented; unit tests assert "neutral value produces the same sound as the gene at rest" |
| 1 | Genome, curve mapping, deterministic PRNG, unit tests | Round-trip normalized↔real passes; same seed gives identical genomes across runs |
| 2 | Growth engine (§5.2-5.7), offline CLI printing branch genomes | Length 0 returns the seed; deviation grows monotonically with length; growth-exempt genes provably never move; guard rails reject silent/unsafe results |
| 3 | Core voice: two oscillators with the documented morph breakpoints, noise with colour, sub-AM, FM, four filter topologies with tracked cutoff, per-voice saturation, shared envelope | Clean, alias-controlled tone; every gene maps to an audible change without clicks; `flt_freq` tracks the keyboard |
| 4 | Modulation: shared envelope with loop period, curvatures, `vol_fade`, two S&H sources, bipolar LFO with `lfo_bal`/`lfo_dly`, drift, per-voice variation, matrix extension | Patches feel alive on static notes; modulation is smooth; S&H steps are not smoothed away |
| 5 | FX and output: chorusing FDN with `rvb_atk` bloom, reverb gating from `Release`, shelves, branch pan, limiter + master clipper | No metallic ringing at defaults; size changes only via direct edits; `Volume` overdrive behaves like the specified curve |
| 6 | Plugin shell: macros, 48 gene parameters, MIDI config, mono/glide, tempo sync, state save/load, note→branch mapping, wheel + `Wheel Target`, rotation | Loads in a DAW; presets recall exactly; automation and MIDI-learn work |
| 7 | Tuning pass with the harness and listening sessions (§13) | A random seed plus any branch is usable most of the time; report the usable-branch ratio |
| 8 | Plant UI and polish | Interaction feels immediate; UI thread never blocks audio |
After each phase report: what was built, what was assumed (list the **[ours]**/**[?]** markers touched), measured results, and open questions — the format already used in `Docs/Implementation-Status.md`. `Docs/Rev3-Alignment-Plan.md` is the sequenced and gated form of these phases (milestones M0–M11) against the current code; keep it updated as they complete.

---

## 15. Open questions

**Rev 2's questions 1, 2, 3, 4, 5, 6, 9, 10, 11 and 12 are closed** by the reference's own documentation — do not re-open them. Answers are tabulated in `Docs/Rev3-ChangeLog.md` §4. The short version: TriggerA-H are CC-bound *actions*; `b_sh`/`mod_sh` are S&H rates; `env_loop` is a loop period; `env_time` is the note duration and key-up stops it; `lfo_bal` is a vibrato/tremolo split; saturation is per-voice with a master clipper after volume; the wheel lerps branches toward full growth; twelve branches per octave, one per semitone, is how the reference works.

**Still open — answer these before implementing the affected behaviour:**

1. **What are `layers` (12 `enabled` flags) and `Bulb Mode` (3 values)?** Present in the patch format and parameter list; probably per-branch enable + plant/voice mode. Out of scope until answered.
2. **What does the asterisk on `lfo_amt` and `adj_clip` mean in the screenshots?** The documented growth-exempt set explains `adj_clip` but not `lfo_amt`. Implement the exempt set as `kFlagNever` regardless; give no other special behaviour to asterisked genes. (Also: the three gene-page screenshots in `Pictures/` have no `.txt` descriptions — worth adding for the record.)
3. **`Wheel Target` has 9 values and `Tempo Sync` has 7**; only the presence and one value ("lfo amount", "growth") are known. Which targets and which sync modes should we offer?
4. **`a_form`'s pulse width**: the morph runs to "pulse" but no PWM gene exists. Is width fixed, or driven by another gene? Assumed fixed 50% for now.
5. **Saturation placement conflict**: the reference describes `saturate` as per-voice and `adj_clip` as a master stage after volume; `Docs/Rev2-Answers.md` A2 says the opposite (per-voice `adj_clip` pre-filter, master `saturate` post-filter). Which is authoritative for GenoMosh? (Rev 3 defaults to the reference's own description.)
6. **Atonality's exact reach**: the reference converges only `a_mod`/`b_mod` toward neutral at generation time; operator answers D7 and D11 pull in different directions. Should our Atonality also cover `lfo_amt`'s vibrato share and `b_freq` ratio-snapping?
7. **Does `a_color` colour one shared noise source or only A's?** There is one colour gene but two noise mixes. Implemented as one shared shaper **[?]**.
8. **Where do the `adj_*` shelves sit relative to the reverb?** Only the clipper's position is documented.
9. **Is `rvb_atk` an envelope on the wet send, or on the reverb's own decay?** "Reverb-amount envelope, active above 0.66" admits both.
10. Target formats: VST3, AU, CLAP, standalone? (VST3 note: v2's face is `Instrument|Synth`, one stereo output bus, 16-channel event buses, `IMidiMapping` + `IMidiLearn` — a sane template.)

---

## 16. Summary of priorities

1. **Curate the gene space** — ranges, curves, neutrals, gates, linkings, sensitivities, and the growth-exempt set. This defines the sound, and §5.7 is the recipe.
2. **Make growth deterministic, continuous, monotonic and safe.**
3. **Build a high-quality, alias-controlled, slightly unstable voice** with a tracking filter and a duration-shaped envelope.
4. **Build a great, evolving chorusing-reverb tail** with bloom and gating.
5. **Build the tooling** — a golden-file protocol, plus per-branch render checks.
6. Only then: UI beauty, plant view, MIDI extras.
