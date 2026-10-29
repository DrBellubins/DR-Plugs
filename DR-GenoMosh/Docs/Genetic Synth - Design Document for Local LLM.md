# Genetic Synth — Design Document

*Working title: Grove. Audience: a local LLM (and its human operator) implementing an original, Synplant-inspired genetic synthesizer plugin in C++/JUCE.*

*Revision 2: sections 4.2, 6, 7 and 8 were revised against screenshots of the reference product's three gene pages. Main changes: the 48 genes are now listed exactly; the amp and modulation envelopes are driven by a shared looping envelope engine (6.1.1) instead of a conventional ADSR plus a third modulator; the LFO targets pitch and volume; and several genes were added or renamed.*

---

## 0. How to use this document

**Purpose.** Describe what to build, why, and where to spend effort, so an implementing model can make good local decisions without guessing the overall intent.

**Ground rules for the implementing model**

1. This is a **clean-room, original implementation**. The parameter names in section 4 come from observing an existing product's interface. They define a *vocabulary and a feature scope*, not code to reproduce. Do not attempt to reproduce proprietary algorithms, presets or assets. Aim for the same *kind* of result through independently designed DSP.
2. Everything labelled **\[inferred\]** is a hypothesis about what a parameter does. Implement the hypothesis, keep it easy to change, and flag it in code comments. Everything labelled **\[unknown\]** must not be implemented until the operator clarifies.
3. **Sound quality beats feature count.** If forced to choose, polish the synth voice, the gene ranges and the reverb before building UI or extras.
4. Real-time safety is mandatory (section 10). No allocation, locks, or I/O on the audio thread.
5. When unsure, build the smallest testable thing, render it offline, and report measurements and what you assumed.

**Out of scope for now:** audio-to-patch matching ("Genopatch"-style analysis), machine learning, MPE, and cloud features.

---

## 1. Product vision

A synthesizer whose sound design happens by *exploration*, not by editing knobs. The player holds a **seed** (a complete patch) and grows **branches**, each a mutated relative of the seed, mapped to musical notes. Playing notes means playing different relatives of one sound. The further a branch grows, the further it strays.

**The target feel**

- Sounds are **alive**: slow drift, interacting modulators, subtle instability, evolving tails.
- Sounds are **related but different**: every branch shares the seed's character at small distances and becomes unpredictable at large ones.
- Exploration is **safe**: random does not mean broken. Silence, clipping, harsh resonance and runaway feedback are filtered out by design.
- Results are **musical by construction**: the parameter space is curated so that most of it is usable.

**Key insight to hold onto:** the genetics is a navigation system over a parameter space. The *character* comes from (a) the synth engine and FX, and (b) the curation of the parameter space: ranges, curves, couplings and sensitivities. A mediocre mutation scheme over a great space sounds good. A great mutation scheme over a bland space does not.

---

## 2. Core concepts and vocabulary

| Term | Meaning in this project |
| --- | --- |
| **Gene** | One normalized parameter value in \[0, 1\] with metadata (curve, range, group, sensitivity). |
| **Genome / DNA** | The ordered vector of all genes that fully defines a patch. The reference product's gene pages show exactly 48 genes (section 4.2). |
| **Seed** | The currently selected genome, the "centre" patch. |
| **Branch** | A derived genome: a deterministic mutation of the seed, parameterized by branch index and distance. Twelve branches, one per pitch class \[inferred\]. |
| **Distance (length)** | How far a branch has grown from the seed, in \[0, 1\]. 0 = identical to seed. |
| **Wheel** | A global macro that scales how open/mutated the whole plant is \[inferred from ModWheel, WheelScale, modWheelOpeningSweep\]. |
| **Rotation** | Rotates which branch is assigned to which note \[inferred\]. |
| **Strand / group** | A subset of genes that mutate coherently (oscillators, filter, modulation, effects) \[inferred\]. |

---

## 3. System architecture

```
   UI thread / message thread                 Audio thread
  ┌───────────────────────────┐        ┌──────────────────────────────────┐
  │ Seed editor, plant view   │        │ MIDI in -> note->branch lookup   │
  │ Genome store (authoritative)│ ---> │ Branch genome snapshot (immutable)│
  │ Branch generator (mutation)│ lock- │ Voice pool (N voices)             │
  │ Preset save/load          │ free   │   each voice: genome -> DSP params│
  └───────────────────────────┘ swap   │ Master FX (chorus -> reverb)     │
                                       │ Output limiter/soft clip         │
                                       └──────────────────────────────────┘
```

**Design rules**

- The **mutation is computed off the audio thread**. When the seed, wheel, branch lengths or rotation change, build 12 resolved branch genomes (plain arrays of floats) and hand them to the audio thread via a lock-free triple buffer or atomic pointer swap.
- A voice **captures its branch genome at note-on** so that dragging a branch does not click already-sounding notes. Optionally crossfade to the new genome over about 50 ms for held notes.
- Resolving genes to actual DSP values (curves, ranges) happens at voice start and per control block, with smoothing.
- Keep the genome as `std::array<float, N>` with a compile-time `N` and an enum of gene IDs, so the mutation code is data-driven by a `GeneInfo` table rather than 48 hand-written lines.

---

## 4. Parameter model

The names below were gathered from the reference product's interface. Descriptions are **\[inferred\]** from names and typical synth conventions unless stated. Sensitivity (S) is the suggested starting mutation scale, from 0 (never mutates) to 1 (mutates freely). These are tuning starting points, not truths. Section 4.2 was revised against screenshots of the reference gene pages: names there are observed, while behaviour is still inferred unless stated.

### 4.1 Macro / performance parameters (host-automatable, *not* mutated)

| ID | Meaning \[inferred\] | Notes |
| --- | --- | --- |
| Volume | Master level | Include output-level compensation. |
| VelSens | Velocity sensitivity | Scales how much velocity affects amp and the modulation envelope. |
| Tuning | Master tune | Cents. |
| Atonality | How freely oscillator tuning may drift away from musical ratios | See 5.5. A mutation-time control, also exposable as a macro. |
| Effect | Macro for effect amount | Likely scales or blends the FX section. |
| ModWheel | Global morph/opening amount | Scales branch distance in real time. |
| WheelScale | Range/scale of the wheel's effect | Sets the maximum effective distance. |
| Rotation | Offsets which branch maps to which note | Integer or continuous (0..12). |
| Branch1 … Branch12 | Per-branch length/distance | The 12 "degrees". Persisted in the patch. |
| TriggerA … TriggerH | **\[unknown\]** Eight trigger slots | Possibly note or sequence triggers. Do not implement until clarified. |

### 4.2 Genes (mutated; the genome)

The reference product's three gene pages (I Envelope and LFO, II Oscillators, III Filter and Effects) show exactly **48 genes**: 16 + 15 + 17. They are listed here by page. Names are observed; meanings are **[inferred]** unless stated. Curves and sensitivities for genes that existed in the first draft are unchanged. Values for newly added genes are fresh starting guesses.

#### Page I: Envelope and LFO (16 genes)

**Shared envelope clock** (drives both envelopes below; see 6.1.1)

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `env_time` | Cycle length of the shared looping clock | exponential, about 20 ms to 10 s | 0.5 |
| `env_loop` | Loop amount: 0 = one-shot, higher = stronger retriggering **[inferred]** | linear | 0.3 |
| `env_tilt` | Skew of each cycle's shape (front or back weighted) | bipolar | 0.4 |
| `env_kf` | Key follow of the cycle length | bipolar | 0.3 |

**Volume envelope**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `vol_atk` | Attack time within a cycle | exponential, about 1 ms to 5 s | 0.5 |
| `vol_dcy` | Decay time within a cycle | exponential | 0.5 |
| `vol_sus` | Sustain level / floor each cycle decays toward | linear | 0.4 |
| `vol_fade` | Fade of peak height across loop repeats, or possibly the release stage **[inferred, uncertain]** | linear | 0.4 |

**Modulation envelope** (drives filter, FM, oscillator mod and oscillator mix)

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `mod_atk` | Attack time within a cycle | exponential | 0.5 |
| `mod_dcy` | Decay time within a cycle | exponential | 0.5 |
| `mod_sh` | Shape (curve) of the modulation envelope | linear | 0.4 |
| `mod_vel` | Velocity to modulation depth | linear | 0.3 |

**LFO** (destinations: pitch and volume; see 6.2)

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `lfo_rate` | Rate | exponential, about 0.02 Hz to 30 Hz | 0.6 |
| `lfo_amt` | Depth (asterisked in the reference, meaning **[unknown]**) | linear/squared | 0.5 |
| `lfo_bal` | Balance between pitch (vibrato) and volume (tremolo) **[inferred]** | bipolar | 0.5 |
| `lfo_dly` | Onset delay before the LFO fades in | exponential, 0 to about 5 s | 0.3 |

#### Page II: Oscillators (15 genes)

**Oscillator A**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `a_form` | Waveform morph (sine through to bright shapes) | linear | 0.5 |
| `a_color` | Timbre control (pulse width, spectral tilt or formant-like colouring) | linear | 0.5 |
| `a_noise` | Noise blend/modulation into the oscillator | squared | 0.4 |
| `a_freq` | Frequency offset relative to the note | semitone/ratio mapped (see 5.5) | 0.4 |
| `a_mod` | Modulation-envelope amount applied to A | bipolar | 0.5 |

**Oscillator B** (the sub/AM control is grouped here in the reference)

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `b_form` | Waveform morph | linear | 0.5 |
| `b_noise` | Noise blend | squared | 0.4 |
| `b_freq` | Frequency offset relative to the note | ratio mapped | 0.4 |
| `b_mod` | Mod-envelope amount applied to B | bipolar | 0.5 |
| `b_sh` | **[inferred, uncertain]** shape/sync/sample-and-hold style control. Start as oscillator sync or hard-sync amount and make it swappable. | linear | 0.4 |
| `sub_am` | Sub-oscillator level and/or amplitude-modulation amount for B | linear | 0.3 |

**Cross-modulation and mix**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `fm_amt` | FM depth, with B modulating A (arrow direction in figure 2b) | squared | 0.5 |
| `fm_mod` | Mod-envelope amount applied to FM depth | bipolar | 0.5 |
| `osc_mix` | Mix between A and B (called `balance` in the first draft) | linear | 0.4 |
| `mix_mod` | Mod-envelope amount applied to the oscillator mix | bipolar | 0.4 |

#### Page III: Filter and Effects (17 genes)

**Filter**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `flt_type` | Filter type: stepped or a morph between LP/BP/HP **[inferred]**. Treat changes as structural and crossfade them (see 5.4). | stepped | 0.3 |
| `flt_freq` | Cutoff | exponential (log Hz) | 0.6 |
| `flt_q` | Resonance | squared, hard-capped | 0.4 |
| `flt_kf` | Key follow | linear | 0.3 |
| `flt_mod` | Mod-envelope to cutoff depth | bipolar | 0.5 |
| `flt_sep` | Separation between two filter stages (dual-filter spread in octaves) **[inferred]** | linear | 0.4 |

**Saturation**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `saturate` | Drive into a soft-clip waveshaper (the reference figure shows a smooth S-curve) | squared | 0.4 |

**Reverb with chorus** (renamed from the first draft's `fx_*`)

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `rvb_size` | Room size / delay-line scale | linear | 0.4 |
| `rvb_len` | Decay time | exponential | 0.4 |
| `rvb_damp` | High-frequency damping | linear | 0.4 |
| `rvb_chor` | Chorus/modulation amount in the tail | linear | 0.4 |
| `rvb_atk` | Reverb onset / fade-in **[inferred, uncertain]** (see 8) | exponential | 0.3 |
| `rvb_mix` | Wet/dry | linear | 0.3 |

**Output adjust** (see 8.1)

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| `adj_bass` | Low-shelf gain | bipolar | 0.2 |
| `adj_treb` | High-shelf gain | bipolar | 0.2 |
| `adj_pan` | Pan | bipolar | 0.2 |
| `adj_clip` | Output soft clip (asterisked in the reference, meaning **[unknown]**) | linear | 0.1 |

The first draft's list of "candidate genes to reach 48" (detune/spread, drift amount, glide, noise colour, stereo width) is dropped. Per-voice variation and slow drift stay as non-gene voice behaviour (section 7).

**Asterisks.** The reference marks `lfo_amt` and `adj_clip` with an asterisk. Its meaning is **[unknown]** (possibly "not mutated" or "scaled by a macro"). Do not implement special behaviour for it until the operator clarifies.

### 4.3 Gene metadata table (implement as data)

```cpp
struct GeneInfo {
    const char* id;          // e.g. "flt_freq"
    Curve       curve;       // Linear, Exp, Squared, Bipolar, Stepped
    float       minValue;    // real-world min
    float       maxValue;    // real-world max
    uint8_t     group;       // coherence group: Env, Amp, Mod, Lfo, OscA, OscB, Mix, Filter, Sat, Fx, Out
    float       sensitivity; // 0..1 mutation scale
    float       softMin;     // preferred musical range inside [0,1] for random walks
    float       softMax;
    Flags       flags;       // Wrap, Reflect, Quantize, NeverMutate
};
```

---

## 5. The genetic engine (the heart of the project)

### 5.1 Requirements

1. **Deterministic**: the same seed, branch index and distance always give the same genome. No fresh randomness at play time.
2. **Continuous**: a small increase in distance gives a small change in sound, so growing a branch is smooth and audible as a gradual morph.
3. **Seed-anchored**: distance 0 returns the seed exactly.
4. **Diverse**: different branches must go in noticeably different directions.
5. **Safe**: results pass guard rails (5.6).
6. **Fast**: computing all 12 branches is cheap (about 12 x N operations); it happens off-thread on any edit.

### 5.2 Branch generation

Each branch has a fixed random **direction vector** derived from a hash of `(seedId, branchIndex)`. The branch genome is the seed displaced along that vector by an amount that depends on distance and per-gene sensitivity.

```cpp
Genome resolveBranch(const Genome& seed, uint64_t seedId, int branch,
                     float distance /*0..1*/, const MutationParams& mp)
{
    Rng rng(hash64(seedId, branch));

    // 1. Per-group coherent directions + per-gene individual directions
    float groupDir[NumGroups];
    for (auto& g : groupDir) g = rng.gaussianClamped(1.0f);   // roughly [-1, 1]

    Genome out;
    for (int g = 0; g < N; ++g) {
        const GeneInfo& gi = kGenes[g];
        float indiv = rng.gaussianClamped(1.0f);
        float dir   = lerp(indiv, groupDir[gi.group], mp.coherence);  // ~0.35..0.6

        // 2. Non-linear distance response: gentle near the seed, wilder far out
        float d = std::pow(distance, mp.distanceExponent);          // ~1.5..2.2

        // 3. Displacement
        float delta = dir * gi.sensitivity * mp.globalScale * d;
        float x = seed[g] + delta;

        // 4. Boundary handling: reflect, don't clamp (clamping piles genes up at 0/1)
        out[g] = reflect01(x);
    }

    // 5. Structural mutations at high distance only (see 5.4)
    applyStructuralMutations(out, rng, distance, mp);

    // 6. Couplings and guard rails (see 5.6)
    applyCouplings(out);
    return out;
}
```

**Why this shape**

- *Group coherence* makes branches sound like cohesive timbral variations instead of 40 independent random walks, which tend to sound like noise.
- *Distance exponent* gives a wide zone of close family resemblance and a fast-growing wild zone.
- *Reflection* avoids parameters sticking at their limits.
- *Per-gene sensitivity* is where taste lives. Tune it by ear, constantly.

### 5.3 Wheel, rotation and branch lengths

- **Effective distance** for branch *i*: `d_i = clamp(branchLength[i] * wheel * wheelScale)`. Re-resolve genomes when any input changes. The wheel is intended to be a live performance control, so make it responsive (smooth on the audio side, resolved off-thread at control rate, about 100 to 200 Hz max).
- **Note mapping**: `branchIndex = (midiNote - rootNote + rotation) mod 12`. Notes that map to the same pitch class in different octaves use the same branch genome. The **seed** is what plays at distance 0.
- **Pitch** is played as normal. The branch changes timbre and modulation, not the played pitch, except through the oscillator offset genes.

### 5.4 Structural mutations (rare, distance-gated)

Continuous nudging is not enough to get the "organic" feel. Add rare discrete changes whose probability rises steeply with distance (e.g. `p = distance^3 * rate`):

- change an oscillator's waveform family or morph region,
- flip a modulation destination's sign,
- switch the sync/FM direction, or step `flt_type` (a gene, but a change of filter topology, so mutate it here with a crossfade rather than continuously),
- swap which modulator (LFO / mod env) drives a destination (see 6.3).

Each must be **crossfaded** in the voice (no clicks).

### 5.5 Atonality and tuning genes

Oscillator frequency genes (`a_freq`, `b_freq`) should map through a **ratio-snapping stage**:

- Compute a candidate offset in semitones from the normalized gene.
- Snap toward a table of musical ratios (octaves, fifths, fourths, thirds, harmonic series entries).
- `Atonality` in \[0, 1\] blends between snapped and free values: 0 = fully snapped (harmonic, consonant), 1 = free (inharmonic, metallic).

This makes random results tuneful by default but lets the user open the door to chaos deliberately.

### 5.6 Guard rails

Implement all of these in `applyCouplings` and in the voice:

- **Level compensation**: estimate loudness from gene values (FM depth, resonance, oscillator mix, noise, saturation, and the average level of the looping amp envelope) and apply gain compensation so branches sit within about 6 dB of the seed.
- **Resonance safety**: cap `flt_q` as a function of `flt_freq`, `flt_type` and `saturate` drive. No self-oscillation unless deliberately allowed.
- **Silence rejection**: if a branch's amp envelope, mix and filter settings imply near-silence (e.g. sustain near zero with instant decay and zero noise, or a loop whose `vol_fade` has silenced it before the note ends), nudge or re-roll with the next seed in the hash chain, up to a bounded retry count.
- **Runaway protection**: soft limiter on the voice and master bus. DC blocker after FM and sync.
- **Range curation**: use `softMin` and `softMax` so most random walks live in the musical middle of each range.
- **Denormal protection** in feedback paths (section 10).

### 5.6.1 Optional: "breeding"

UI-level evolution is simply: user drags a branch to grow it; choosing a branch means **planting** it, so that branch's genome becomes the new seed (`seedId` changes, branch lengths reset or decay). Optional extensions are crossover between two genomes (per-group pick or blend) and "mutate this seed by X%".

---

## 6. Modulation system

### 6.1 Sources

- **Looping envelope engine** (6.1.1): produces both the amp envelope (`vol_*`) and the modulation envelope (`mod_*`) from one shared clock (`env_*`).
- **LFO** (`lfo_*`), free-running or note-synced, with an onset delay (`lfo_dly`), optional per-voice phase offset, and a slow-drift variant.
- **Velocity**, **key follow**, **mod wheel / wheel macro**.
- **Noise/random walk**: a slow smoothed random source for drift (voice-level life, not a gene).

### 6.1.1 Looping envelope engine (revised, **[inferred]**)

**Observation.** In the reference interface, figures 1a (volume) and 1b (modulation) both draw their envelope repeating across the note. The four `env_*` knobs sit above both envelopes rather than inside either one. The working hypothesis is that the reference has **no conventional ADSR**: its envelopes are LFO-like looping envelopes, which keep a held note moving instead of only shaping the press and release. This replaces the earlier draft's separate "third modulator" for `env_*`.

**Design.**

- One **cycle clock** per voice. Cycle length `T = env_time * 2^(-env_kf * (note - 60) / 12)`, so `env_kf` is key follow of the cycle length (0 = none, positive = higher notes cycle faster). **[inferred]**
- `vol_atk`/`vol_dcy` and `mod_atk`/`mod_dcy` shape what happens *inside* each cycle. The shared clock decides *when* a new cycle begins. If attack plus decay is longer than `T`, the cycle is cut off by the restart.
- `env_loop`: 0 gives a one-shot envelope (it plays once and settles at `vol_sus`, behaving like an ordinary envelope). Above 0, each new cycle re-excites the envelope toward its peak by that amount while the note is held. Whether the reference treats `env_loop` as a repeat count, a loop region or a depth is **[unknown]**; start with the depth interpretation and keep it swappable.
- `env_tilt`: warps the normalized cycle phase before the attack/decay shape is applied, weighting the cycle toward its front or back.
- `vol_fade` **[inferred, uncertain]**: scales the peak of each repeat so peaks step down across the loop, as figure 1a suggests. Figure 1b shows the modulation envelope's peaks staying roughly level, so apply the fade to the amp envelope only. It may instead be the release stage; keep it swappable.
- `vol_sus`: the floor each cycle decays toward, and the held level when `env_loop` is 0.
- `mod_sh`: shape of the modulation envelope's decay; `mod_vel`: velocity to modulation depth.
- **Note-off behaviour is [unknown].** Static figures cannot show it. Until it is clarified, stop looping at note-off and play a short fixed release (about 50 to 300 ms), and flag it in code.

```cpp
// Per voice, per control block. Every name here is a hypothesis (see above).
float T = envTime * std::exp2(-envKf * (note - 60) / 12.0f);
phase += blockSeconds / T;
if (phase >= 1.0f && noteHeld && envLoop > 0.0f) {
    phase -= 1.0f;
    ++cycleIndex;                                    // a new repeat begins
}
float p       = tiltWarp(std::min(phase, 1.0f), envTilt);
float peak    = std::pow(1.0f - volFade, cycleIndex);        // amp envelope only
float volEnv  = peak * adShape(p, volAtk, volDcy, volSus);
float modEnv  = adShape(p, modAtk, modDcy, modSh);
```

### 6.2 Destinations

Oscillator frequency, oscillator form/colour, FM amount, oscillator mix (`mix_mod`), filter cutoff, filter separation, effect parameters.

The LFO's destinations are **pitch (vibrato) and volume (tremolo)**, per figures 1c and 1d. `lfo_bal` most likely sets the split between them **[inferred]**. The earlier draft's "oscillator A vs B" and "filter" LFO destinations are dropped. `lfo_amt` is marked with an asterisk in the reference whose meaning is **[unknown]**; treat it as an ordinary depth gene.

### 6.3 The organic extension: sparse modulation matrix as genes

The reference parameter set uses fixed per-destination modulation-envelope amounts (`a_mod`, `b_mod`, `fm_mod`, `flt_mod`, `mix_mod`) and a fixed pitch/volume LFO split. To get closer to *evolving* modulation, treat these as a **matrix of weights** where mutation can:

1. nudge existing weights (continuous),
2. rarely switch a destination's source between the modulation envelope and the LFO (structural),
3. rarely add or remove a routing.

Cap active routings (e.g. 6 to 10) so patches stay legible. Keep amounts bipolar and squared so small values stay subtle.

### 6.4 Smoothing

All modulation targets are smoothed (about 5 to 20 ms, one-pole or linear ramp) except envelope-driven targets that rely on fast attack. Use sample-accurate envelopes and block-rate or sub-block-rate (e.g. 16 to 32 samples) control updates for the rest.

---

## 7. Voice architecture

```
 note -> pitch -> [Osc A] ------------------------------\
                  [Osc B + sub_am] --(FM: B -> A)-------+--> osc_mix (+ mix_mod) --> mix
                  [Noise] -> a_noise / b_noise (into each oscillator)
 mix --> dual filter (flt_type, flt_freq, flt_sep, flt_q, flt_kf)
     --> saturate --> amp env (vol_*, driven by the looping engine) --> chorus/reverb (rvb_*)
     --> output stage (adj_bass, adj_treb, adj_pan, adj_clip)
```
Placement of `saturate` and the `adj_*` stage is **[inferred]** from the page order of the reference interface.

**Oscillators**

- Band-limited (PolyBLEP/BLAMP, wavetable with mip-maps, or oversampled). Aliasing is the most common reason "digital" synths sound cheap, so spend effort here.
- `*_form` morphs through waveforms smoothly: no zipper noise and no audible discontinuities.
- FM must remain stable and alias-managed: consider 2x to 4x oversampling on the FM path only. FM direction is B modulating A; `fm_amt` sets depth and `fm_mod` the mod-envelope amount applied to it.

**Filter**

- Zero-delay-feedback state-variable filter or ladder with mild saturation. Two stages with `flt_sep` spreading their cutoffs; `flt_type` selects or morphs between LP/BP/HP **[inferred]**; crossfade type changes.
- Key follow in octaves per octave.

**Saturation**

- `saturate` drives a soft-clip waveshaper after the filter **[inferred placement]**. Oversample 2x and follow it with a DC blocker.

**Voice-level life**

- Per-voice random offsets (tiny detune, phase start, drift phase, mod-time offsets), seeded per note-on.
- Slow drift on pitch (a few cents) and cutoff. This is a large part of the "organic" feel.

**Polyphony and stealing**: 8 to 16 voices, release-aware stealing, and a short fade on stolen voices.

---

## 8. Effects (reverb with chorus) and output stage

Genes: `rvb_size`, `rvb_len`, `rvb_damp`, `rvb_chor`, `rvb_atk`, `rvb_mix` (macro `Effect`). Output-stage genes: `adj_bass`, `adj_treb`, `adj_pan`, `adj_clip` (see 8.1).

**Recommended architecture**

- **Chorus/modulation stage** (before or inside the reverb): two to four modulated delay taps with slow incommensurate LFOs, stereo-decorrelated. `rvb_chor` scales depth and rate together.
- **Reverb**: 8-line feedback delay network (FDN) with a Householder or Hadamard mixing matrix.
  - `rvb_size` scales the base delay lengths (use mutually prime lengths).
  - `rvb_len` sets decay via per-line feedback gain (RT60-based).
  - `rvb_damp` sets a one-pole or shelf low-pass in each line.
  - `rvb_atk` **[inferred, uncertain]**: reverb onset. Start as a fade-in of the wet send (and/or pre-delay) over `rvb_atk`, so the tail blooms instead of starting instantly. Keep it swappable.
  - Slightly modulate delay lengths to prevent metallic ringing.
  - Add short input diffusion (allpass chain) for density.
- **Mix**: equal-power wet/dry. `Effect` scales `rvb_mix` and optionally other `rvb_*` genes together.
- The tail should **evolve with the timbre** because the `rvb_*` genes mutate with the branch. Ensure delay-length changes are smooth, or use two reverb instances crossfaded when a large size jump occurs.

### 8.1 Output stage (`adj_*`) **[inferred]**

The reference product's third page groups four "adjust" genes after the reverb. Placement after the reverb is inferred from page order.

- `adj_bass`, `adj_treb`: low and high shelf gains, bipolar, small range (about plus or minus 9 dB), low sensitivity so branches keep a similar tonal balance.
- `adj_pan`: equal-power pan, bipolar, low sensitivity. Per-voice variation of pan is allowed on top of it.
- `adj_clip`: soft-clip ceiling or drive for the output. The reference marks it with an asterisk whose meaning is **[unknown]**; implement it as a plain soft clipper and do not give it special behaviour.

---

## 9. MIDI and host integration

Observed config items (per-instance settings, not patch genes):

| Setting | Notes |
| --- | --- |
| MIDIReceiveChannel | "any" or 1 to 16. |
| MIDITransmitChannel | "none" or 1 to 16 (likely for plant interaction -> MIDI out; optional). |
| PitchWheelRange | Semitones. |
| VelocityCurve | soft / standard / hard: three monotonic curves. |
| SupportProgramChanges, SelectWithMIDI | Optional: map program change to presets or seeds. |
| CC mapping | Per-parameter MIDI CC assignment, persisted. |

Additional rules:

- Expose macro parameters (Volume, Tuning, Effect, ModWheel, Rotation, Atonality, VelSens, WheelScale) as host-automatable `AudioProcessorValueTreeState` parameters.
- Genes are **internal state**, saved in the patch, not exposed one-by-one for automation (except possibly a few via an advanced mode). Branch lengths are persisted and optionally automatable.
- Persist `seedId`, genome, branch lengths, rotation, wheel state and MIDI config.

---

## 10. Real-time and engineering constraints

- **Language/framework:** modern C++ (17+), JUCE.
- **Audio thread:** no heap allocation, no locks, no file/IO, no `std::function` construction, no exceptions. Preallocate everything in `prepareToPlay`.
- **Genome hand-off:** immutable snapshot swapped via a lock-free structure (e.g. atomic pointer with a small ring of preallocated snapshots).
- **Denormals:** `juce::ScopedNoDenormals` plus explicit tiny DC/noise in long feedback paths where needed.
- **Sample-rate independence:** every time constant and filter coefficient derives from the actual sample rate. Test at 44.1, 48, 96 kHz.
- **Determinism:** the PRNG must be platform-independent (e.g. xoshiro/splitmix64 with your own gaussian approximation) so presets sound identical across machines. Do not use `std::normal_distribution` (implementation-defined).
- **Parameter smoothing** everywhere an audible zipper could occur.
- **Unit-testable core:** the genome, mutation and mapping code must be pure and independent of JUCE audio classes so it can run in a test harness.

---

## 11. State and presets

- Serialize with a **versioned** format (e.g. JSON or ValueTree XML with a `version` field). Include `seedId` (64-bit), the full genome (floats), 12 branch lengths, wheel, rotation, atonality, FX macro, MIDI settings.
- Keep the mutation algorithm **versioned too**: changing sensitivities or hash inputs changes every saved branch. If you must change it, bump a `mutationVersion` and keep the old code path.
- Provide init and randomize-seed functions (randomize = random genome within `softMin`/`softMax`).

---

## 12. Optional UI: the plant view

The reference interface has an extensive generative-plant visual vocabulary (core size, root/branch/twig parameters, hue, alpha, sway, decay, twist, hover and drag states, a rotation handle, a mod-wheel arc). These are **presentation only**. Suggested minimal approach:

- Central seed; 12 radial branches at 30-degree intervals offset by `rotation`.
- Branch length on screen = `branchLength[i]`; colour hue derived from the genome (e.g. hash or a few key genes like `flt_freq`, `a_form`).
- Twigs: a few recursive sub-branches with deterministic pseudo-random bends, so the plant is a visual fingerprint of the genome.
- Gentle idle sway and a decay/fade for unused branches.
- Interactions: drag branch to grow it, drag rotation handle, wheel arc for the mod wheel, click seed to re-plant, hover shows note names.

Build this **after** the audio engine and a plain debug UI (12 sliders plus wheel and rotation) work well.

---

## 13. Testing and tuning workflow

Tuning the gene table is the project's real work, so build tools early.

**Offline render harness (command-line)**

- Input: seed ID or genome, branch index, distance, note, velocity. Output: WAV.
- Batch mode: render all 12 branches at distances 0.0, 0.25, 0.5, 0.75, 1.0.

**Automated checks per render**

- Peak and RMS level, loudness relative to the seed (target within about 6 dB).
- Silence detection, NaN/Inf detection, DC offset.
- Spectral centroid and flatness range, to catch branches that are all-noise or all-dull.
- Aliasing estimate (energy above the expected harmonic series at high notes).
- Reproducibility test: same inputs give bit-identical output.

**Human evaluation loop**

1. Generate many random seeds. Listen to branches at several distances.
2. Log which genes cause "bad" results; adjust `softMin`/`softMax`, sensitivity and couplings.
3. Compare character to reference recordings of the target aesthetic (not to copy, but to calibrate liveliness, density and tail behaviour).

---

## 14. Implementation phases (with acceptance criteria)

| Phase | Deliverable | Done when |
| --- | --- | --- |
| 1 | Genome, `GeneInfo` table, curve mapping, deterministic PRNG, unit tests | Round-trip normalized<->real values pass; same seed gives identical genomes across runs. |
| 2 | Mutation engine (5.2 to 5.6), offline CLI that prints branch genomes | Distance 0 returns seed; monotonic growth of deviation with distance; guard rails reject silent/unsafe results. |
| 3 | Core voice: 2 oscillators, FM, noise, sub, dual filter with `flt_type`, saturate, amp env, mod env | Clean, alias-controlled tone; all genes map to audible changes without clicks. |
| 4 | Modulation: shared looping envelope engine (6.1.1), LFO with `lfo_dly`, drift, per-voice variation, matrix extension | Patches feel alive with static notes; modulation is smooth. |
| 5 | FX: chorus + FDN reverb with smooth parameter changes, plus the `adj_*` output stage | No metallic ringing at default; size/length changes are click-free. |
| 6 | Plugin shell: JUCE processor, macros, MIDI config, state save/load, note->branch mapping, wheel, rotation | Loads in a DAW; presets recall exactly; automation works. |
| 7 | Tuning pass using the harness and listening sessions | A random seed plus any branch is usable most of the time. |
| 8 | Plant UI and polish | Interaction feels immediate; UI thread never blocks audio. |

After each phase the implementing model should produce: what was built, what was assumed (list the **\[inferred\]** items touched), measured results, and open questions.

---

## 15. Open questions for the human operator

1. What exactly do **TriggerA to TriggerH** do in the reference product? (Not implemented until answered.)
2. Is `b_sh` sync, sample-and-hold, or a shape control? Prefer measuring the reference sound if possible.
3. Is the wheel a global morph scalar as assumed, or does it also reshape individual branches?
4. What polyphony and CPU budget is acceptable?
5. Should branch lengths be automatable and MIDI-learnable?
6. Is the goal strictly 12 branches per octave, or should the layout be extensible?
7. Target formats: VST3 and AU only, or also CLAP and standalone?
8. What does the asterisk on `lfo_amt` and `adj_clip` in the reference mean? (Not implemented until answered.)
9. Does the reference amp envelope have a release stage, and does looping stop at note-off? Is `vol_fade` the loop fade or the release?
10. Is `env_loop` a repeat count, a loop depth, or a loop region?
11. Is `lfo_bal` a pitch vs. volume split, as figures 1c and 1d suggest?
12. Where exactly do `saturate` and the `adj_*` stage sit in the reference signal chain?

---

## 16. Summary of priorities

1. **Curate the gene space** (ranges, curves, sensitivities, couplings). This defines the sound.
2. **Make mutation deterministic, continuous and safe.**
3. **Build a high-quality, alias-controlled, slightly unstable voice** with lively modulation.
4. **Build a great, evolving reverb/chorus tail.**
5. **Build tooling** for offline rendering and automated sanity checks.
6. Only then: UI beauty, MIDI extras and optional features.
