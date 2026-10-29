In @DR-GenoMosh/Docs/Rev2-Alignment-Plan.md
the decisions and their answers:

D1: option b, should be kept for later if wanted to be used.
D2: strict + extras
D3: start with option a, keep it swappable with the others
D4: option a
D5: stop looping + 180ms release (release may be a custom param in the future)
D6: the synplant screenshots explicitely show both temolo and vibrato at the same LFO rate, but with different amounts
D7: atonality should not effect pitch movement
D8: gene with a new Curve::Stepped, trait path removed
D9: yes, but only when the user is dragging a branch in the UI
D10: current initially, leave option for continous
D11: atonlity should be a blend, with it affecting the amount of vibrato (and other things affecting pitch), but it should not be a gene
D12: keep removed, its a UI semantic I don't care about
D13: Unchanged (more on distortion in A2)
D14: keep, leaving the option for hybrid (shown in A1 below)
D15: break them
D16: 16, unchanged, but with a real fade on stolen voices


Addendums:
A1: Hybrid: keep directions on the wire, add a per-branch cache in Synth keyed by (snapshot.revision, quantised distance). Note-on becomes a lookup in the common case, recomputing only when the plant or the distance actually changed — ~15 lines, gives you the doc's "voice captures a plain array of floats" verbatim without giving up the live wheel.

A2:
Synplant has a per-voice soft-clipper with an adj_clip gene, it is per-voice pre-filter saturation with asymmetry. After that there is a saturate parameter that controls post-filter saturation on all voices, but before reverb.