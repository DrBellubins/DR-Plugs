// GenoMosh editor front end.
//
// The whole UI is here; the audio engine, the genome and the plant are in C++.
// The protocol (also documented in Docs/Web-UI.md):
//
//   in  ui.controls {controls:[{id,label,min,max}]}   what the UI may edit
//       ui.state    {params{..}, lengths[..], wheel, wheelScale, active, seedId, octave}
//       ui.plant    {branches:[{hue,sat,bri,reach,strokes[..]}]}
//       ui.genes    {groups:[name], genes:[{i,label,g,v}]}
//       ui.keys     {hint}
//   out ui.ready {}
//       ui.param    {id, value}        normalised 0..1
//       ui.gene     {index, value}     normalised 0..1
//       ui.note     {note, velocity}   velocity 0 = note off
//       ui.newSeed / ui.plantBranch {branch} / ui.requestGenes
//       ui.key      {ch, down}

import { hasBackend, onEvent, sendEvent } from "./bridge.js";

// Any error in a message handler would otherwise be silent: the JUCE back end
// never sees the exception. Forwarding it makes the plugin log it.
window.addEventListener("error", (event) => {
  sendEvent("ui.error", { message: `${event.message} at ${event.filename}:${event.lineno}` });
});
import { PlantRenderer, kNumBranches, kRingRadius } from "./plant.js";

const element = (id) => document.getElementById(id);
const noteNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const kNoteForSlot = (slot) => 60 + slot;

// Same note gating the native plant view used: a clicked slot sounds while the
// pointer is held, a dragged one only while the branch is actually changing.
const kDragThreshold = 0.03;    // of ring radius: click becomes drag
const kMoveThreshold = 0.012;   // of ring radius: still moving
const kIdleReleaseMs = 250.0;

const state = {
  controls: [],
  ranges: new Map(),
  values: new Map(),    // id -> normalised value as last seen from the backend
  lengths: new Array(kNumBranches).fill(0.45),
  wheel: 1.0,
  wheelScale: 0.85,
  octave: 0,
  showingGenes: false,
  selectedBranch: -1,
  playing: null,        // { note, slot } currently held by this view
};

// ------------------------------------------------------------------ controls

function formatValue(range, normalised) {
  const value = range.min + (range.max - range.min) * normalised;
  if (range.id === "tuning") return `${value >= 0 ? "+" : ""}${value.toFixed(0)} ct`;
  return value.toFixed(2);
}

function makeKnob(host, descriptor) {
  const wrapper = document.createElement("div");
  wrapper.className = "control";
  const name = document.createElement("div");
  name.className = "name";
  name.textContent = descriptor.label;
  const body = document.createElement("div");
  body.className = "knob" + (descriptor.live ? " live" : "");
  body.innerHTML = `<svg viewBox="0 0 100 100">
      <circle class="arc-track" cx="50" cy="50" r="36" fill="none" stroke-width="6"
              stroke-linecap="round" pathLength="100" stroke-dasharray="75 100"
              transform="rotate(127.5 50 50)"/>
      <circle class="arc-value" cx="50" cy="50" r="36" fill="none" stroke-width="6"
              stroke-linecap="round" pathLength="100" stroke-dasharray="0 100"
              transform="rotate(127.5 50 50)"/>
      <line class="pointer" x1="50" y1="50" x2="50" y2="20" stroke-width="2.5"
            stroke-linecap="round" transform="rotate(0 50 50)"/>
    </svg>`;
  const readout = document.createElement("div");
  readout.className = "value";
  wrapper.appendChild(name);
  wrapper.appendChild(body);
  wrapper.appendChild(readout);
  host.appendChild(wrapper);

  const arc = body.querySelector(".arc-value");
  const pointer = body.querySelector(".pointer");

  const control = {
    id: descriptor.id,
    suppressUntil: 0,
    setValue(normalised, fromBackend) {
      if (fromBackend && performance.now() < control.suppressUntil) return;
      state.values.set(descriptor.id, normalised);
      arc.setAttribute("stroke-dasharray", `${(normalised * 75).toFixed(1)} 100`);
      pointer.setAttribute("transform", `rotate(${(-135 + normalised * 270).toFixed(1)} 50 50)`);
      readout.textContent = formatValue(descriptor, normalised);
    },
  };

  let dragging = false;
  let startValue = 0;
  let startY = 0;

  body.addEventListener("pointerdown", (event) => {
    dragging = true;
    startValue = state.values.get(descriptor.id) ?? 0;
    startY = event.clientY;
    body.setPointerCapture(event.pointerId);
    control.suppressUntil = performance.now() + 900;
    event.preventDefault();
  });

  body.addEventListener("pointermove", (event) => {
    if (!dragging) return;
    const fine = event.shiftKey ? 0.25 : 1.0;
    const next = Math.min(1, Math.max(0, startValue + ((startY - event.clientY) / 180) * fine));
    control.setValue(next, false);
    sendEvent("ui.param", { id: descriptor.id, value: next });
  });

  const endDrag = (event) => {
    if (!dragging) return;
    dragging = false;
    try {
      body.releasePointerCapture(event.pointerId);
    } catch (error) {
      // The pointer is already gone; nothing to release.
    }
    control.suppressUntil = performance.now() + 150;
  };

  body.addEventListener("pointerup", endDrag);
  body.addEventListener("pointercancel", endDrag);

  return control;
}

function makeSlider(host, descriptor) {
  const wrapper = document.createElement("div");
  wrapper.className = "control";
  const slider = document.createElement("div");
  slider.className = "slider";
  const input = document.createElement("input");
  input.type = "range";
  input.min = "0";
  input.max = "1";
  input.step = "0.001";
  const readout = document.createElement("div");
  readout.className = "value";
  slider.appendChild(input);
  wrapper.appendChild(slider);
  wrapper.appendChild(readout);
  host.appendChild(wrapper);

  input.title = descriptor.label;

  const control = {
    id: descriptor.id,
    setValue(normalised) {
      if (document.activeElement !== input) input.value = String(normalised);
      state.values.set(descriptor.id, normalised);
      readout.textContent = formatValue(descriptor, normalised);
    },
  };

  input.addEventListener("input", () => {
    const value = Number(input.value);
    state.values.set(descriptor.id, value);
    readout.textContent = formatValue(descriptor, value);
    sendEvent("ui.param", { id: descriptor.id, value });
  });

  return control;
}

const controls = new Map();

function buildControlSlots() {
  const layout = [
    { id: "atonality", host: "slot-atonality", kind: "knob", tip:
      "How much the plant is allowed to affect tonality. 0 pins the pitch capable genes to no effect, 1 leaves them free. Rebuilds the plant; never retunes a note you are holding." },
    { id: "effect", host: "slot-effect", kind: "knob", live: true, tip: "Scales the chorus / reverb wet path of whatever is playing." },
    { id: "tuning", host: "slot-tuning", kind: "knob", live: true, tip: "Master tune in cents." },
    { id: "wheelScale", host: "slot-wheelScale", kind: "slider", tip:
      "How far the mod wheel (CC1) opens the plant. At 0 every branch plays the seed; at 1 the branch lengths are used in full." },
    { id: "velSens", host: "slot-velSens", kind: "slider", tip: "How much playing harder changes loudness and the modulation envelope." },
    { id: "volume", host: "slot-volume", kind: "slider", tip: "Master output level." },
  ];

  for (const entry of layout) {
    const descriptor = state.ranges.get(entry.id) || { id: entry.id, label: entry.id, min: 0, max: 1 };
    const host = element(entry.host);
    const control = entry.kind === "knob" ? makeKnob(host, descriptor) : makeSlider(host, descriptor);
    control.setValue(state.values.get(entry.id) ?? 0.5, false);
    host.title = entry.tip;
    controls.set(entry.id, control);
  }
}

// --------------------------------------------------------------------- plant

const plant = new PlantRenderer(element("plant"));
let pointer = null;

function slotAt(x, y) {
  const distance = Math.sqrt(x * x + y * y);
  if (distance > kRingRadius * 1.25) return -1;
  const segment = (Math.PI * 2) / kNumBranches;
  let slot = Math.round((Math.atan2(y, x) - Math.PI / 2) / segment);
  slot %= kNumBranches;
  if (slot < 0) slot += kNumBranches;
  return slot;
}

function svgPoint(event) {
  const svg = element("plant");
  const point = svg.createSVGPoint();
  point.x = event.clientX;
  point.y = event.clientY;
  const ctm = svg.getScreenCTM();
  if (!ctm) return { x: 0, y: 0 };
  const mapped = point.matrixTransform(ctm.inverse());
  return { x: mapped.x, y: mapped.y };
}

function playNote(note, velocity) {
  sendEvent("ui.note", { note, velocity });
}

function endPlaying() {
  if (!state.playing) return;
  playNote(state.playing.note, 0);
  plant.setSounding(state.playing.slot, false);
  state.playing = null;
}

function setBranchLength(slot, length) {
  const clamped = Math.min(1, Math.max(0, length));
  state.lengths[slot] = clamped;
  plant.setLengths(state.lengths);
  sendEvent("ui.param", { id: `branch${slot}`, value: clamped });
}

function wirePlant() {
  const svg = element("plant");

  const distanceOf = (event) => {
    const point = svgPoint(event);
    return Math.sqrt(point.x * point.x + point.y * point.y) / kRingRadius;
  };

  svg.addEventListener("pointerdown", (event) => {
    if (event.button === 2) return;
    const point = svgPoint(event);
    const slot = slotAt(point.x, point.y);
    if (slot < 0) return;

    svg.setPointerCapture(event.pointerId);
    pointer = {
      slot,
      distance: distanceOf(event),
      lastDistance: distanceOf(event),
      dragging: false,
      lastMove: performance.now(),
    };
    state.selectedBranch = slot;
    plant.setGrabbed(slot, true);
    plant.push(slot, 0.25);

    const note = kNoteForSlot(slot);
    endPlaying();
    state.playing = { note, slot };
    plant.setSounding(slot, true);
    playNote(note, 0.85);
    event.preventDefault();
  });

  svg.addEventListener("pointermove", (event) => {
    const point = svgPoint(event);
    const slot = slotAt(point.x, point.y);
    for (let branchIndex = 0; branchIndex < kNumBranches; ++branchIndex)
      plant.setHovered(branchIndex, branchIndex === slot);

    if (!pointer) return;

    const distance = distanceOf(event);
    const moved = Math.abs(distance - pointer.lastDistance);

    if (Math.abs(distance - pointer.distance) > kDragThreshold) pointer.dragging = true;

    if (moved > kMoveThreshold) {
      pointer.lastDistance = distance;
      pointer.lastMove = performance.now();
      plant.push(pointer.slot, (distance - pointer.lastDistance) * 6 + 0.08);

      if (pointer.dragging && !state.playing) {
        const note = kNoteForSlot(pointer.slot);
        state.playing = { note, slot: pointer.slot };
        plant.setSounding(pointer.slot, true);
        playNote(note, 0.85);
      }
    }

    if (pointer.dragging) setBranchLength(pointer.slot, distance);
  });

  const release = (event) => {
    if (!pointer) return;
    plant.setGrabbed(pointer.slot, false);
    plant.setHovered(pointer.slot, false);
    endPlaying();
    pointer = null;
    try {
      svg.releasePointerCapture(event.pointerId);
    } catch (error) {
      // Pointer already released.
    }
  };

  svg.addEventListener("pointerup", release);
  svg.addEventListener("pointercancel", release);

  svg.addEventListener("contextmenu", (event) => {
    const point = svgPoint(event);
    const slot = slotAt(point.x, point.y);
    if (slot < 0) return;
    event.preventDefault();
    state.selectedBranch = slot;
    sendEvent("ui.plantBranch", { branch: slot });
    plant.push(slot, 1.4);
  });

  // A dragged branch stops sounding once the pointer goes still.
  setInterval(() => {
    if (!pointer || !pointer.dragging || !state.playing) return;
    if (performance.now() - pointer.lastMove < kIdleReleaseMs) return;
    endPlaying();
  }, 60);
}

// ----------------------------------------------------------------- gene page

const groupColours = ["#7fd08a", "#8ec7e8", "#e8c07f", "#c79fe8", "#7fd0c8", "#e88f8f",
  "#a8c77f", "#e8a87f", "#8fb0e8", "#d08fb8"];

function buildGenePage(payload) {
  const grid = element("geneGrid");
  grid.replaceChildren();

  const groups = document.createElement("div");
  grid.appendChild(groups);
  grid.style.columns = "2";

  const perGroup = new Map();
  for (const gene of payload.genes) {
    if (!perGroup.has(gene.g)) perGroup.set(gene.g, []);
    perGroup.get(gene.g).push(gene);
  }

  let columnIndex = 0;
  for (const [groupIndex, genes] of perGroup) {
    const section = document.createElement("section");
    section.className = "gene-group";
    section.style.columnBreak = "auto";

    const heading = document.createElement("h2");
    heading.textContent = payload.groups[groupIndex] || `group ${groupIndex}`;
    section.appendChild(heading);

    for (const gene of genes) {
      const row = document.createElement("div");
      row.className = "gene-row";
      row.style.setProperty("--gene-colour", groupColours[groupIndex % groupColours.length]);

      const name = document.createElement("span");
      name.className = "gene-name";
      name.textContent = gene.label;

      const slider = document.createElement("input");
      slider.type = "range";
      slider.min = "0";
      slider.max = "1";
      slider.step = "0.001";
      slider.value = String(gene.v);
      slider.title = gene.label;

      slider.addEventListener("input", () => {
        sendEvent("ui.gene", { index: gene.i, value: Number(slider.value) });
      });

      row.appendChild(name);
      row.appendChild(slider);
      section.appendChild(row);
    }

    grid.appendChild(section);
    ++columnIndex;
  }
}

// -------------------------------------------------------------------- pages

function showPage(showGenes) {
  state.showingGenes = showGenes;
  element("plantHost").hidden = showGenes;
  element("sideColumn").hidden = showGenes;
  element("genePage").hidden = !showGenes;
  element("pageLabel").textContent = showGenes ? "gene table" : "seed / branches";
  element("pageButton").textContent = showGenes ? "plant" : "genes";
  element("body").classList.toggle("genes-page", showGenes);

  if (showGenes) sendEvent("ui.requestGenes");
  else plant.start();
}

// ------------------------------------------------------------------ messages

onEvent("ui.controls", (payload) => {
  state.controls = payload.controls || [];
  for (const descriptor of state.controls) state.ranges.set(descriptor.id, descriptor);
  buildControlSlots();
});

onEvent("ui.state", (payload) => {
  if (payload.params) {
    for (const [id, value] of Object.entries(payload.params)) {
      const control = controls.get(id);
      if (control) control.setValue(value, true);
      else state.values.set(id, value);
    }
  }
  if (payload.lengths) {
    for (let branchIndex = 0; branchIndex < kNumBranches; ++branchIndex) {
      if (pointer && pointer.slot === branchIndex) continue; // never fight the drag
      state.lengths[branchIndex] = payload.lengths[branchIndex];
    }
    plant.setLengths(state.lengths);
  }
  if (typeof payload.wheel === "number") {
    state.wheel = payload.wheel;
    plant.setWheel(payload.wheel);
  }
  state.wheelScale = payload.wheelScale ?? state.wheelScale;
  plant.setActive(payload.active ?? -1);

  element("wheelReadout").textContent =
    `wheel ${state.wheel.toFixed(2)}  x ${state.wheelScale.toFixed(2)}  = ${(state.wheel * state.wheelScale).toFixed(2)} open`;

  if (payload.seedId) {
    element("seedLabel").textContent = `seed ${String(payload.seedId).slice(-8)}`;
  }
  if (typeof payload.octave === "number") state.octave = payload.octave;
});

onEvent("ui.plant", (payload) => {
  plant.setPlant(payload);
});

onEvent("ui.genes", (payload) => {
  buildGenePage(payload);
});

onEvent("ui.keys", (payload) => {
  const hint = payload.hint || "";
  element("keyHint").textContent = state.octave === 0 ? hint : `${hint}   octave ${state.octave}`;
});

// ----------------------------------------------------------------------- start

function wireButtons() {
  element("newSeedButton").addEventListener("click", () => {
    sendEvent("ui.newSeed", {});
    state.selectedBranch = -1;
  });

  element("plantBranchButton").addEventListener("click", () => {
    const branch = state.selectedBranch;
    if (branch >= 0) sendEvent("ui.plantBranch", { branch });
  });

  element("pageButton").addEventListener("click", () => showPage(!state.showingGenes));
}

function wireKeyboard() {
  const handle = (event, down) => {
    if (event.repeat) return;
    if (event.target instanceof HTMLInputElement && !down) return;
    if (event.key.length !== 1) return;
    sendEvent("ui.key", { ch: event.key, down });
  };

  document.addEventListener("keydown", (event) => handle(event, true));
  document.addEventListener("keyup", (event) => handle(event, false));
}

function loadMockData() {
  // No backend: a browser preview of the plant, so the animation can be tuned
  // without launching the plugin.
  const branches = [];
  for (let branchIndex = 0; branchIndex < kNumBranches; ++branchIndex) {
    const strokes = [];
    let reach = 0;
    const grow = (from, angle, length, depth) => {
      if (depth <= 0 || length < 0.02) return;
      const to = [from[0] + Math.cos(angle) * length, from[1] + Math.sin(angle) * length];
      const control = [from[0] + Math.cos(angle + 0.1) * length * 0.5, from[1] + Math.sin(angle + 0.1) * length * 0.5];
      strokes.push([from[0], from[1], control[0], control[1], to[0], to[1], depth]);
      reach = Math.max(reach, Math.hypot(to[0], to[1]));
      for (let child = 0; child < 2; ++child) {
        grow(to, angle + (child === 0 ? -1 : 1) * (0.3 + Math.random() * 0.45), length * 0.55, depth - 1);
      }
    };
    grow([0, 0], Math.PI / 2 + branchIndex * (Math.PI * 2 / kNumBranches), 1, 4);
    branches.push({ hue: branchIndex / 12, sat: 0.6, bri: 0.8, reach, strokes });
  }
  plant.setPlant({ branches });
  state.lengths = new Array(kNumBranches).fill(0.6);
  plant.setLengths(state.lengths);
  element("keyHint").textContent = "keys  z s x c v b n m   q 2 w e 5 r t 6 y u   , .  octave";
}

wirePlant();
wireButtons();
wireKeyboard();
plant.start();

if (hasBackend) {
  sendEvent("ui.ready", {});
} else {
  loadMockData();
}

document.addEventListener("visibilitychange", () => {
  if (document.hidden) plant.stop();
  else if (!state.showingGenes) plant.start();
});
