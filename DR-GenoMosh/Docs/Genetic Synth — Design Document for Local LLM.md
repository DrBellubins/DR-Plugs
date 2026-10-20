# Genetic Synth — Design Document

*Working title: Grove. Audience: a local LLM (and its human operator) implementing an original, Synplant-inspired genetic synthesizer plugin in C++/JUCE.*

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
| **Genome / DNA** | The ordered vector of all genes that fully defines a patch. Observed products of this type use a few dozen genes (about 48). |
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

The names below were gathered from the reference product's interface. Descriptions are **\[inferred\]** from names and typical synth conventions unless stated. Sensitivity (S) is the suggested starting mutation scale, from 0 (never mutates) to 1 (mutates freely). These are tuning starting points, not truths.

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

**Amplitude envelope**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| vol\_atk | Attack time | exponential, \~1 ms to \~5 s | 0.5 |
| vol\_dcy | Decay time | exponential | 0.5 |
| vol\_sus | Sustain level | linear | 0.4 |

Also add a fixed or semi-fixed release stage. Name TBD (not seen in the parameter list).

**Modulation envelope** (drives filter, FM and oscillator mod)

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| mod\_atk | Attack time | exponential | 0.5 |
| mod\_dcy | Decay time | exponential | 0.5 |
| mod\_sh | Shape (curve) or sustain/hold of the mod envelope | linear | 0.4 |
| mod\_vel | Velocity -> modulation depth | linear | 0.3 |

**Looping/tilting envelope** \[inferred, treat as a third modulator\]

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| env\_time | Envelope duration | exponential | 0.5 |
| env\_loop | Loop behaviour/amount (loop point or loop on/off blend) | linear | 0.3 |
| env\_tilt | Skew of the envelope shape (front/back weighted) | bipolar | 0.4 |

**LFO**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| lfo\_rate | Rate | exponential, \~0.02 Hz to \~30 Hz | 0.6 |
| lfo\_amt | Depth | linear/squared | 0.5 |
| lfo\_bal | Balance of LFO destination split (e.g. oscillator A vs B, or pitch vs filter) | bipolar | 0.5 |

**Oscillator A**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| a\_form | Waveform morph (sine through to bright shapes) | linear | 0.5 |
| a\_color | Timbre control (pulse width, spectral tilt or formant-like colouring) | linear | 0.5 |
| a\_noise | Noise blend/modulation into the oscillator | squared | 0.4 |
| a\_freq | Frequency offset relative to the note | semitone/ratio mapped (see 5.5) | 0.4 |
| a\_mod | Modulation-envelope amount applied to A | bipolar | 0.5 |

**Oscillator B**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| b\_form | Waveform morph | linear | 0.5 |
| b\_noise | Noise blend | squared | 0.4 |
| b\_freq | Frequency offset relative to the note | ratio mapped | 0.4 |
| b\_mod | Mod-envelope amount applied to B | bipolar | 0.5 |
| b\_sh | **\[inferred, uncertain\]** shape/sync/sample-and-hold style control. Start as oscillator sync or hard-sync amount and make it swappable. | linear | 0.4 |

**Mix and cross-modulation**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| balance | Mix between A and B | linear | 0.4 |
| fm\_amt | FM depth (B modulating A, or vice versa) | squared | 0.5 |
| fm\_mod | Mod-envelope/LFO amount applied to FM depth | bipolar | 0.5 |
| sub\_am | Sub-oscillator level and/or amplitude-modulation amount | linear | 0.3 |

**Filter**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| flt\_freq | Cutoff | exponential (log Hz) | 0.6 |
| flt\_q | Resonance | squared, hard-capped | 0.4 |
| flt\_kf | Key follow | linear | 0.3 |
| flt\_mod | Mod-envelope -> cutoff depth | bipolar | 0.5 |
| flt\_sep | Separation between two filter stages (dual-filter spread in octaves) \[inferred\] | linear | 0.4 |

**Effects (reverb with chorus/modulation)**

| Gene | Meaning | Curve | S |
| --- | --- | --- | --- |
| fx\_size | Room size / delay-line scale | linear | 0.4 |
| fx\_len | Decay time | exponential | 0.4 |
| fx\_damp | High-frequency damping | linear | 0.4 |
| fx\_chors | Chorus/modulation amount in the tail | linear | 0.4 |
| fx\_mix | Wet/dry | linear | 0.3 |

This is about 38 genes. If reaching \~48, candidates are: oscillator detune/spread, per-voice drift amount, saturation drive, release time, stereo width, noise colour, glide, and an extra LFO destination or two. Add them **only when they audibly widen the sonic space**.

### 4.3 Gene metadata table (implement as data)

```cpp
struct GeneInfo {
    const char* id;          // e.g. "flt_freq"
    Curve       curve;       // Linear, Exp, Squared, Bipolar, Stepped
    float       minValue;    // real-world min
    float       maxValue;    // real-world max
    uint8_t     group;       // coherence group: OscA, OscB, Filter, Mod, Amp, Lfo, Fx, Mix
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
- switch the sync/FM direction or the filter mode,
- swap which modulator (LFO / mod env / loop env) drives a destination (see 6.3).

Each must be **crossfaded** in the voice (no clicks).

### 5.5 Atonality and tuning genes

Oscillator frequency genes (`a_freq`, `b_freq`) should map through a **ratio-snapping stage**:

- Compute a candidate offset in semitones from the normalized gene.
- Snap toward a table of musical ratios (octaves, fifths, fourths, thirds, harmonic series entries).
- `Atonality` in \[0, 1\] blends between snapped and free values: 0 = fully snapped (harmonic, consonant), 1 = free (inharmonic, metallic).

This makes random results tuneful by default but lets the user open the door to chaos deliberately.

### 5.6 Guard rails

Implement all of these in `applyCouplings` and in the voice:

- **Level compensation**: estimate loudness from gene values (FM depth, resonance, oscillator mix, noise) and apply gain compensation so branches sit within about 6 dB of the seed.
- **Resonance safety**: cap `flt_q` as a function of `flt_freq` and drive. No self-oscillation unless deliberately allowed.
- **Silence rejection**: if a branch's amp envelope, mix and filter settings imply near-silence (e.g. sustain near zero with instant decay and zero noise), nudge or re-roll with the next seed in the hash chain, up to a bounded retry count.
- **Runaway protection**: soft limiter on the voice and master bus. DC blocker after FM and sync.
- **Range curation**: use `softMin` and `softMax` so most random walks live in the musical middle of each range.
- **Denormal protection** in feedback paths (section 10).

### 5.6.1 Optional: "breeding"

UI-level evolution is simply: user drags a branch to grow it; choosing a branch means **planting** it, so that branch's genome becomes the new seed (`seedId` changes, branch lengths reset or decay). Optional extensions are crossover between two genomes (per-group pick or blend) and "mutate this seed by X%".

---

## 6. Modulation system

### 6.1 Sources

- **Amp envelope** (`vol_*`).
- **Mod envelope** (`mod_*`), per voice, velocity-scaled by `mod_vel`.
- **Loop/tilt envelope** (`env_*`), per voice, optionally looping.
- **LFO** (`lfo_*`), free-running or note-synced, with slow-drift variant and optional per-voice phase offset.
- **Velocity**, **key follow**, **mod wheel / wheel macro**.
- **Noise/random walk**: a slow smoothed random source for drift.

### 6.2 Destinations

Oscillator frequency, oscillator form/colour, FM amount, filter cutoff, filter separation, balance, effect parameters (size/damping/chorus).

### 6.3 The organic extension: sparse modulation matrix as genes

The reference parameter set uses fixed per-destination amounts (`a_mod`, `b_mod`, `fm_mod`, `flt_mod`). To get closer to *evolving* modulation, treat these as a **matrix of weights** where mutation can:

1. nudge existing weights (continuous),
2. rarely switch a destination's source (structural),
3. rarely add or remove a routing.

Cap active routings (e.g. 6 to 10) so patches stay legible. Keep amounts bipolar and squared so small values stay subtle.

### 6.4 Smoothing

All modulation targets are smoothed (about 5 to 20 ms, one-pole or linear ramp) except envelope-driven targets that rely on fast attack. Use sample-accurate envelopes and block-rate or sub-block-rate (e.g. 16 to 32 samples) control updates for the rest.

---

## 7. Voice architecture

```
 note -> pitch -> [Osc A] ---\
                 [Osc B] ----+--> balance --> (FM / sync cross-mod) --> mix
                 [Sub / AM] -/
                 [Noise] ------> into osc and/or mix
 mix --> dual filter (flt_freq, flt_sep, flt_q, flt_kf) --> amp env --> pan --> master FX
```

**Oscillators**

- Band-limited (PolyBLEP/BLAMP, wavetable with mip-maps, or oversampled). Aliasing is the most common reason "digital" synths sound cheap, so spend effort here.
- `*_form` morphs through waveforms smoothly: no zipper noise and no audible discontinuities.
- FM must remain stable and alias-managed: consider 2x to 4x oversampling on the FM path only.

**Filter**

- Zero-delay-feedback state-variable filter or ladder with mild saturation. Two stages with `flt_sep` spreading their cutoffs; allow LP/BP/HP morph if useful.
- Key follow in octaves per octave.

**Voice-level life**

- Per-voice random offsets (tiny detune, phase start, drift phase, mod-time offsets), seeded per note-on.
- Slow drift on pitch (a few cents) and cutoff. This is a large part of the "organic" feel.

**Polyphony and stealing**: 8 to 16 voices, release-aware stealing, and a short fade on stolen voices.

---

## 8. Effects: reverb with chorus

Genes: `fx_size`, `fx_len`, `fx_damp`, `fx_chors`, `fx_mix` (macro `Effect`).

**Recommended architecture**

- **Chorus/modulation stage** (before or inside the reverb): two to four modulated delay taps with slow incommensurate LFOs, stereo-decorrelated. `fx_chors` scales depth and rate together.
- **Reverb**: 8-line feedback delay network (FDN) with a Householder or Hadamard mixing matrix.
  - `fx_size` scales the base delay lengths (use mutually prime lengths).
  - `fx_len` sets decay via per-line feedback gain (RT60-based).
  - `fx_damp` sets a one-pole or shelf low-pass in each line.
  - Slightly modulate delay lengths to prevent metallic ringing.
  - Add short input diffusion (allpass chain) for density.
- **Mix**: equal-power wet/dry. `Effect` scales `fx_mix` and optionally other fx genes together.
- The tail should **evolve with the timbre** because FX genes mutate with the branch. Ensure delay-length changes are smooth, or use two reverb instances crossfaded when a large size jump occurs.

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
| 3 | Core voice: 2 oscillators, FM, noise, sub, dual filter, amp env, mod env | Clean, alias-controlled tone; all genes map to audible changes without clicks. |
| 4 | Modulation: loop env, LFO, drift, per-voice variation, matrix extension | Patches feel alive with static notes; modulation is smooth. |
| 5 | FX: chorus + FDN reverb with smooth parameter changes | No metallic ringing at default; size/length changes are click-free. |
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

---

## 16. Summary of priorities

1. **Curate the gene space** (ranges, curves, sensitivities, couplings). This defines the sound.
2. **Make mutation deterministic, continuous and safe.**
3. **Build a high-quality, alias-controlled, slightly unstable voice** with lively modulation.
4. **Build a great, evolving reverb/chorus tail.**
5. **Build tooling** for offline rendering and automated sanity checks.
6. Only then: UI beauty, MIDI extras and optional features.
