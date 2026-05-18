
## Core design
- [x] 8 allpass diffusion filters (delay + reverb tunings)
- [x] Diffusion Quality (quality-distributed taps, no tail shortening)
- [x] Diffusion Size (runtime smoothing / pitch warp)
- [x] Diffusion Amount crossfade (0→0.5 delay, 0.5→1 reverb)
- [x] Delay Time (ms mode, 1–1000ms)
- [x] Beat-sync modes (nrm, trip, dot)
- [x] Delay time smoothing
- [x] Low Pass / High Pass filters (pre/post)
- [x] Stereo Spread
- [x] Damping filter
- [x] Feedback gain
- [ ] Ping-ping L/R implementation
- [ ] Ping-ping M/S implementation

## Pitch shifting
- [x] Up/Down/Random sequence modes
- [x] Octave-only pitch change (no mid-echo pitch change)
- [x] Pitch UI (range slider, enable checkbox, mode dropdown)
- [x] Pitch audible in reverb signal
- [ ] Phase Vocoder backend (high quality)
- [ ] Backend quality dropdown

## Distortion
- [ ] Heat (S-shaped clipper)
- [ ] Chebyshev shaper
- [ ] Hard Clipper
- [ ] Diode
- [ ] Tube Distortion (close to fabfilter saturn)
- [ ] Tape Distortion (close to fabfilter saturn)