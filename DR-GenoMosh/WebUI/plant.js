// The plant: the ring is the keyboard, the branches are the pitch classes.
//
// The C++ side owns the shape: it grows the tree in unit space (Source/Web/
// PlantGeometry.cpp) and sends the strokes plus their measured reach. This file
// only ever moves what it was given, which is what keeps the picture on screen
// and the plant in the audio engine the same plant.
//
// Everything animated here is a deformation of that authoritative geometry:
//   * wind        a bend that grows with distance from the seed, two
//                 incommensurate sines so no two branches move together,
//   * touch       a damped spring: grabbing, dragging and releasing twang,
//   * sound       a note pumps energy into its branch, which decays,
//   * growth      the length parameter is followed smoothly, never jumped to.

const kNumBranches = 12;
const kRingRadius = 92;          // viewBox units, matches the disc in index.html
const kMinReach = 0.10;          // of the ring radius, as in the audio side
const kMaxReach = 0.92;
const kNoteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const kHalfPi = Math.PI / 2;
const kTwoPi = Math.PI * 2;

// Wind. Deliberately small: the plant is never still, but it must never look
// like it is being blown about either.
const kWindBase = 0.018;         // radians at the tip
const kWindProfile = 1.6;        // how fast the bend builds out towards the tip
const kGustBeats = [0.31, 0.113, 0.047]; // Hz, prime-ish so they never realign

// Spring response for grabbing / dragging / releasing a branch.
const kSpringStiffness = 120.0;
const kSpringDamping = 9.0;

function hueToRgb(hue, saturation, brightness) {
  const hueSector = (p, q, t) => {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6) return p + (q - p) * 6 * t;
    if (t < 1 / 2) return q;
    if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
    return p;
  };
  if (saturation === 0) {
    const grey = Math.round(brightness * 255);
    return `rgb(${grey},${grey},${grey})`;
  }
  const q = brightness < 0.5 ? brightness * (1 + saturation) : brightness + saturation - brightness * saturation;
  const p = 2 * brightness - q;
  const to255 = (t) => Math.round(hueSector(p, q, t) * 255);
  return `rgb(${to255(hue + 1 / 3)},${to255(hue)},${to255(hue - 1 / 3)})`;
}

export class PlantRenderer {
  constructor(svgRoot) {
    this.scene = svgRoot.querySelector("#plantScene");
    this.branches = [];
    this.wheel = 1.0;
    this.active = -1;
    this.running = false;
    this.time = 0;
    this.lastFrame = 0;
    this.buildSkeleton();
  }

  // The disc, the seed and one group per pitch class. Built once; only the
  // path data and the handle radius ever change after this.
  buildSkeleton() {
    const disc = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    disc.setAttribute("class", "disc");
    disc.setAttribute("r", String(kRingRadius));
    this.scene.appendChild(disc);

    for (let branchIndex = 0; branchIndex < kNumBranches; ++branchIndex) {
      const angle = kHalfPi + branchIndex * (kTwoPi / kNumBranches);
      const group = document.createElementNS("http://www.w3.org/2000/svg", "g");
      group.setAttribute("class", "branch");
      group.dataset.branch = String(branchIndex);

      const twigs = document.createElementNS("http://www.w3.org/2000/svg", "g");
      twigs.setAttribute("class", "twigs");

      const handle = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      handle.setAttribute("class", "handle");
      handle.setAttribute("r", "4.5");

      const hit = document.createElementNS("http://www.w3.org/2000/svg", "circle");
      hit.setAttribute("class", "handle-hit");
      hit.setAttribute("r", "13");

      const label = document.createElementNS("http://www.w3.org/2000/svg", "text");
      label.setAttribute("class", "slot-label");
      label.textContent = kNoteNames[branchIndex];

      group.appendChild(twigs);
      group.appendChild(handle);
      group.appendChild(hit);
      group.appendChild(label);
      this.scene.appendChild(group);

      this.branches.push({
        angle,
        group,
        twigs,
        handle,
        hit,
        label,
        paths: [],
        strokes: [],
        reach: 1,
        colour: "#888888",
        growth: 0.45,
        growthShown: 0.45,
        bend: 0,
        bendVelocity: 0,
        bendTarget: 0,
        pop: 0,
        popVelocity: 0,
        popTarget: 0,
        energy: 0,
        phase: branchIndex * 2.39996, // golden angle: no two branches in step
        grabbed: false,
        hovered: false,
        sounding: false,
      });
    }

    const seed = document.createElementNS("http://www.w3.org/2000/svg", "circle");
    seed.setAttribute("class", "seed-dot");
    seed.setAttribute("r", "3.6");
    this.scene.appendChild(seed);
  }

  // ------------------------------------------------------------- geometry in

  // payload from the backend: { branches: [{hue,sat,bri,reach,strokes}] }
  setPlant(payload) {
    if (!payload || !Array.isArray(payload.branches)) return;

    for (let branchIndex = 0; branchIndex < this.branches.length; ++branchIndex) {
      const branch = this.branches[branchIndex];
      const source = payload.branches[branchIndex];
      if (!source) continue;

      const colour = hueToRgb(source.hue, source.sat, source.bri);
      branch.colour = colour;
      branch.strokes = source.strokes;
      branch.reach = Math.max(1e-3, source.reach);

      while (branch.twigs.firstChild) branch.twigs.removeChild(branch.twigs.firstChild);
      branch.paths = branch.strokes.map((stroke) => {
        const path = document.createElementNS("http://www.w3.org/2000/svg", "path");
        const depth = Math.max(1, stroke[6]);
        path.setAttribute("stroke-width", String(depth * 0.62));
        path.setAttribute("stroke", colour);
        path.setAttribute("opacity", String(0.4 + 0.5 * (depth / 4)));
        branch.twigs.appendChild(path);
        return path;
      });

      branch.handle.setAttribute("fill", colour);
      branch.group.style.setProperty("--glow", colour);
    }
    this.draw();
  }

  setLengths(lengths) {
    for (let branchIndex = 0; branchIndex < this.branches.length; ++branchIndex) {
      const value = Number(lengths[branchIndex]);
      if (Number.isFinite(value)) this.branches[branchIndex].growth = Math.min(1, Math.max(0, value));
    }
  }

  setWheel(value) {
    this.wheel = Math.min(1, Math.max(0, value));
  }

  setActive(branchIndex) {
    this.active = branchIndex;
  }

  setSounding(branchIndex, on) {
    const branch = this.branches[branchIndex];
    if (!branch) return;
    branch.sounding = on;
    branch.group.classList.toggle("sounding", on);
    if (on) branch.energy = Math.min(1.6, branch.energy + 1.0); // a note feeds the leaves
  }

  setHovered(branchIndex, on) {
    const branch = this.branches[branchIndex];
    if (!branch) return;
    branch.hovered = on;
    branch.group.classList.toggle("hovered", on);
  }

  setGrabbed(branchIndex, on) {
    const branch = this.branches[branchIndex];
    if (!branch) return;
    branch.grabbed = on;
    branch.group.classList.toggle("grabbed", on);
    if (on) {
      branch.popTarget = 0.05;   // reaching for the pointer
      branch.bendTarget = 0.05;  // and leaning away from the seed
    } else {
      branch.popTarget = 0;
      branch.bendTarget = 0;     // the spring carries the twang from here
      branch.popVelocity += 0.9;
    }
  }

  // A shove along the direction the pointer moved, used while dragging.
  push(branchIndex, amount) {
    const branch = this.branches[branchIndex];
    if (!branch) return;
    branch.bendVelocity += amount;
    branch.popVelocity += Math.abs(amount) * 0.4;
  }

  ringRadius() {
    return kRingRadius;
  }

  angleOf(branchIndex) {
    return this.branches[branchIndex].angle;
  }

  // ---------------------------------------------------------------- animation

  start() {
    if (this.running) return;
    this.running = true;
    this.lastFrame = performance.now();
    const step = (now) => {
      if (!this.running) return;
      const dt = Math.min(0.05, (now - this.lastFrame) / 1000);
      this.lastFrame = now;
      this.time += dt;
      this.integrate(dt);
      this.draw();
      requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }

  stop() {
    this.running = false;
  }

  integrate(dt) {
    const step = Math.min(dt, 1 / 30);
    for (const branch of this.branches) {
      // Growth is followed, never jumped to: host automation and a fast drag
      // both read as growing rather than as popping.
      branch.growthShown += (branch.growth - branch.growthShown) * Math.min(1, 8 * step);

      // Damped springs keep the motion organic without any keyframes.
      const bendForce = kSpringStiffness * (branch.bendTarget - branch.bend) - kSpringDamping * branch.bendVelocity;
      branch.bendVelocity += bendForce * step;
      branch.bend += branch.bendVelocity * step;

      const popForce = kSpringStiffness * (branch.popTarget - branch.pop) - kSpringDamping * branch.popVelocity;
      branch.popVelocity += popForce * step;
      branch.pop += branch.popVelocity * step;

      branch.energy = Math.max(0, branch.energy - step * 0.9);
    }
  }

  // Bend angle at a normalised radius, per branch. Sum of three slow sines.
  windAt(branch, radiusRatio) {
    const t = this.time;
    let sum = 0;
    for (let index = 0; index < kGustBeats.length; ++index) {
      const rate = kGustBeats[index] * kTwoPi;
      const weight = index === 0 ? 1.0 : index === 1 ? 0.55 : 0.3;
      sum += weight * Math.sin(t * rate + branch.phase * (index + 1));
    }
    const amplitude = kWindBase * (1 + 0.5 * branch.energy);
    return amplitude * Math.pow(radiusRatio, kWindProfile) * sum;
  }

  draw() {
    const t = this.time;

    for (let branchIndex = 0; branchIndex < this.branches.length; ++branchIndex) {
      const branch = this.branches[branchIndex];

      // Same rule as the audio side: min reach .. max reach of the ring, and
      // the wheel scales how far the plant is allowed to open.
      const open = branch.growthShown * this.wheel;
      const target =
        kRingRadius * (kMinReach + (kMaxReach - kMinReach) * open) * (1 + branch.pop);
      const scale = target / branch.reach;
      const breath = 1 + 0.012 * Math.sin(t * 0.7 + branch.phase);

      for (let strokeIndex = 0; strokeIndex < branch.strokes.length; ++strokeIndex) {
        const stroke = branch.strokes[strokeIndex];
        const depth = Math.max(1, stroke[6]);
        // The further out a twig sits, the more it flutters.
        const flutter = 0.012 * ((4 - depth) / 3) * Math.sin(t * (2.2 + 0.3 * (strokeIndex % 7)) + strokeIndex * 1.7);

        const path = branch.paths[strokeIndex];
        if (!path) continue;

        const x0 = stroke[0], y0 = stroke[1];
        const cx = stroke[2], cy = stroke[3];
        const x1 = stroke[4], y1 = stroke[5];

        const p0 = this.place(branch, x0, y0, scale, breath, 0);
        const pc = this.place(branch, cx, cy, scale, breath, 0.6 + flutter);
        const p1 = this.place(branch, x1, y1, scale, breath, 1.0 + flutter);

        path.setAttribute("d", `M${p0[0].toFixed(2)},${p0[1].toFixed(2)} Q${pc[0].toFixed(2)},${pc[1].toFixed(
          2)} ${p1[0].toFixed(2)},${p1[1].toFixed(2)}`);
      }

      // The growth handle sits on the branch's own spoke, and shows the length
      // the parameter asks for even while the drawing is still catching up.
      const handleRadius = kRingRadius * (0.3 + (kMaxReach - 0.3) * branch.growthShown);
      const hx = Math.cos(branch.angle) * handleRadius;
      const hy = Math.sin(branch.angle) * handleRadius;
      branch.handle.setAttribute("cx", hx.toFixed(2));
      branch.handle.setAttribute("cy", hy.toFixed(2));
      branch.handle.setAttribute("r", (branch.grabbed ? 7 : branch.hovered ? 6 : 4.5).toFixed(2));
      branch.hit.setAttribute("cx", hx.toFixed(2));
      branch.hit.setAttribute("cy", hy.toFixed(2));

      branch.label.setAttribute("x", (Math.cos(branch.angle) * (kRingRadius + 16)).toFixed(2));
      branch.label.setAttribute("y", (Math.sin(branch.angle) * (kRingRadius + 16)).toFixed(2));
      branch.label.classList.toggle("active", branchIndex === this.active);

      branch.twigs.style.filter = branch.sounding || branch.energy > 0.05
        ? `drop-shadow(0 0 ${(1.5 + 3.5 * Math.min(1, branch.energy)).toFixed(2)}px var(--glow))`
        : "";
      branch.handle.setAttribute("fill", branchIndex === this.active ? "#ffffff" : branch.colour);
    }
  }

  // One point of the unit tree, deformed: the backend grows every tree along
  // +x, so the branch's own spoke angle is the base rotation, and the wind and
  // the grab bend add to it. Then the whole thing scales into the ring.
  place(branch, x, y, scale, breath, weight) {
    const radius = Math.sqrt(x * x + y * y);
    const ratio = Math.min(1.4, (radius * scale) / kRingRadius);
    const wind = this.windAt(branch, ratio) * weight + branch.bend * Math.pow(ratio, 0.7) * weight;
    const angle = branch.angle + wind;

    if (wind === 0 && branch.angle === 0) {
      return [x * scale * breath, y * scale * breath];
    }
    const cosine = Math.cos(angle);
    const sine = Math.sin(angle);
    return [(x * cosine - y * sine) * scale * breath, (x * sine + y * cosine) * scale * breath];
  }
}

export { kNumBranches, kRingRadius, kNoteNames };
