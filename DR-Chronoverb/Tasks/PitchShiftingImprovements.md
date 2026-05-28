Now I have a thorough understanding of the entire pitch shifting architecture. Let me provide the analysis.
Analysis of Granular Pitch Shifting in DR-Chronoverb
Context Summary

Your pitch shifter lives inside a delay/reverb feedback loop. The OctaveEchoPitchShifter drives a GranularPitchBackend which uses 4 overlapping Hann-windowed read heads with cubic interpolation, reading from a small (300ms) circular buffer. Pitch ratios change only at echo boundaries (once per delay period). The sequences operate in integer octaves only.
Part 1: Structural / Architectural Changes (Highest to Lowest Impact)

1. Decouple the pitch shifter's internal buffer from its own write pointer — read directly from the delay line instead.

Currently, GranularPitchBackend maintains its own private 300ms circular buffer and writes every incoming sample into it. This means it has a copy of a small window of audio, completely disconnected from the delay line's much larger buffer. This is the root cause of several problems: the lookback window is tiny (300ms max), the read heads can only "see" recently written samples, and higher ratios (which consume audio faster) quickly exhaust the available material, causing the grains to loop over the same tiny section repeatedly. If the backend instead received a read accessor to the main DelayLine buffer, grains could look back much further into history (up to 1000ms), giving higher-ratio shifts far more source material and dramatically reducing the repetitive, incoherent quality.

2. Move from integer-octave sequences to semitone-resolution pitch ratios.

Currently semitonesToOctaveIndex() rounds semitones to the nearest whole octave (round(semitones / 12.0f)), so +24 semitones → octave index 2, and the ratio is pow(2, 2) = 4.0. But +13 semitones also rounds to octave 1 — identical to +12. The user-facing range allows ±48 semitones, but internally only 9 distinct values exist (-4 to +4 octaves). Switching to pow(2.0f, semitones / 12.0f) for actual semitone-accurate ratios would give you the full pitch range with fine control. This is a fundamental limitation that makes the pitch parameter behave nothing like what users expect.

3. Increase the ratio clamp ceiling and adapt grain parameters dynamically based on ratio.

SetInitialRatio hard-clamps to [0.25, 4.0], meaning anything above +24 semitones (ratio 4.0) is silently clamped. But even ratios approaching 4.0 are problematic because the grain read heads advance at ratio samples per sample — at ratio 4.0, a 50ms grain's worth of source material is consumed in 12.5ms, so the 4 read heads are cycling through very short, repetitive sections. The fix involves dynamically scaling grainLengthSamples and lookbackMultiplier upward as ratio increases, so that higher shifts have proportionally larger grains and deeper lookback windows. This directly addresses issue #1 (incoherence above +24).

4. Replace rand() with a deterministic, seedable PRNG.

generateJitterSamples() uses rand(), which is global, non-thread-safe, and non-deterministic across runs. This is a direct contributor to issue #2 (inconsistent character on repeated playback). Every time you play the same loop, the jitter pattern is different, so grains select slightly different regions of the source material. Replacing this with a local std::mt19937 or a simple LCG, seeded from a deterministic value (like the echo boundary count or the host transport position), would make grain behavior repeatable for the same input.

5. Tie the grain phase / echo boundary to host transport position.

The echo boundary counter (echoWriteCounterL) free-runs from the moment of PrepareToPlay. It has no relationship to the host's playback position. If you reset the counter and grain phases when the host transport starts (using juce::AudioPlayHead position info), the grain sequence would be identical on every playback of the same section — directly addressing issue #2.
Part 2: Improvements for Quality, Naturalness, and High-Ratio Performance (Highest to Lowest Impact)

1. Dynamic grain length scaling with ratio.

Currently grain length is fixed at 50ms regardless of pitch ratio. At ratio 4.0, each grain only covers 12.5ms of unique source material before wrapping. At ratio 0.5, each grain stretches across 100ms of source.
Improvement: Scale grain length inversely with ratio: effectiveGrainLength = baseGrainLength / max(ratio, 0.5f) (clamped to a reasonable range). This ensures that at high ratios, each grain covers roughly the same duration of source material as at ratio 1.0, eliminating the "metallic / ring-modulated" sound at high pitches caused by extremely short effective windows.

2. Increase the number of overlapping grains for high ratios.

4 Hann-windowed grains at 0/25/50/75% phase offsets provide a constant-power sum (≈2.0) at ratio 1.0, but at high ratios the read heads race ahead and de-correlate rapidly, causing amplitude modulation artifacts. Adding 2–4 additional grains (6 or 8 total) when the ratio exceeds ~2.0 would fill in the gaps and smooth out the output. The Hann window normalization divisor would need to adjust accordingly.

3. Use pitch-synchronous grain triggering (or approximation thereof) for transient preservation.

The current grain trigger is purely phase-based: every grainLengthSamples the phase wraps and a new grain starts. This is oblivious to the structure of the input signal. A simple envelope follower (a one-pole filter on the absolute value of the input) could detect when the input energy rises sharply (a transient). When a transient is detected, you can force-reset all grain phases so they all start reading from the transient onset, rather than from wherever the lookback + jitter happened to land. This "snaps" grains to transient boundaries and preserves attack character.

4. Implement a "freshness bias" in the lookback anchor.

When a grain resets (phase wraps to 0), anchorHeadToWrite places it at writeIndex - grainLength * lookbackMultiplier + jitter. This lookback is fixed and doesn't account for the ratio. At high ratios, the read head races far ahead of its initial anchor before the next reset, potentially reading past the write head into stale data (the buffer is circular). A better anchor would be: writeIndex - max(grainLength * lookbackMultiplier, grainLength * ratio * safetyMargin), ensuring read heads never overtake the write head even at high ratios.

5. Replace Hann windowing with a Tukey (tapered cosine) window.

The Hann window attenuates the center of each grain to zero at the edges, which works well for smooth tonal signals but aggressively softens transients that happen to fall near a grain boundary. A Tukey window with a configurable taper ratio (e.g., 0.3 — meaning 70% of the grain is at full amplitude, with 15% cosine fade-in and fade-out) would preserve more of the original signal energy and transient shape while still crossfading cleanly between grains.

6. Add a short crossfade between the old grain and the new grain at reset.

Currently when a read head's phase wraps, it jumps instantly to a new anchor position. Even though the Hann window should be near zero at this point, floating-point phase accumulation means the window value isn't exactly zero, causing micro-discontinuities. A short (1–2ms) crossfade between the dying grain's tail and the new grain's start would eliminate these clicks entirely, particularly noticeable at high ratios where wraps happen more frequently.

7. Apply a lightweight one-pole lowpass filter to the granular output, scaled by ratio.

Higher pitch ratios amplify the spectral artifacts of granular synthesis (the "chimney effect" — spectral copies appear at multiples of the grain rate). A gentle lowpass at min(sampleRate/2, originalNyquist / ratio) on the output removes these spectral images without dulling the sound at lower ratios. At ratio 1.0 the filter is wide open; at ratio 4.0 it rolls off above ~5.5kHz at 48kHz sample rate, which is where the artifacts live.

8. Use overlapping grain pairs that read from slightly offset positions (micro-detuning).

Instead of 4 independent grains all reading from the same anchor with tiny jitter, group them into 2 pairs where each pair's members are offset by 1–3ms. This creates a natural chorus effect that masks the "sameness" of the granular texture and makes the output sound richer and more organic, particularly beneficial for sustained/tonal input where the repetitive grain pattern is most audible.

9. Smoothly ramp the ratio change over the boundary crossfade duration.

Currently OnEchoBoundary copies the entire GrainState, changes the ratio on the copy, and crossfades the output of old and new states. This means during the crossfade, you hear two pitch-shifted signals at two different ratios summed together — this creates a momentary "detuned" sound rather than a smooth pitch glide. Instead, linearly interpolating the ratio itself over the crossfade duration (using the same cosine curve) would produce a continuous pitch sweep that sounds far more natural.

10. Pre-roll grain states at initialization.

When the backend is first created or reset, all 4 grain phases start at 0.0/0.25/0.5/0.75 and all read heads are anchored to the same write position (which contains silence). The first few grains therefore produce silence or near-silence mixed with the first arriving audio. "Pre-rolling" the grain phases by running a few hundred samples of silence through processStateOneSample after reset would establish the staggered grain pattern before real audio arrives, avoiding the soft/muffled onset of the first echo.