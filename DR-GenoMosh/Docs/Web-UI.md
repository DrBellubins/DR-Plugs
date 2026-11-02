# The web editor

Since 2026-11-02 the whole UI is a web page. The JUCE widget editor (PlantView,
GeneGrid, ThemedKnob, the `layout()`/`paint()` pair) is gone; what is left is a
`juce::WebBrowserComponent` that fills the window.

## Where things live

| Path | What it is |
|---|---|
| `WebUI/index.html` | Structure: header, plant host, control slots, gene page, output strip. |
| `WebUI/style.css` | The palette, mirroring `Source/Utils/Theme.h` (grey plates = live controls). |
| `WebUI/app.js` | Controls, the gene page, note gating, the computer keyboard, the message protocol. |
| `WebUI/plant.js` | The plant: rendering the geometry it is given + all animation. |
| `WebUI/bridge.js` | `onEvent` / `sendEvent` over `window.__JUCE__.backend`. |
| `Source/Web/EditorWebView.{h,cpp}` | The back end: resource provider, event listeners, state publisher. |
| `Source/Web/PlantGeometry.{h,cpp}` | The authoritative plant picture, in unit space. |
| `Source/PluginEditor.{h,cpp}` | A host for the view; it owns nothing else. |
| `Source/UI/ComputerKeyboard.{h,cpp}` | Still the only key map; the page forwards typed characters to it. |

`CMakeLists.txt` embeds `WebUI/*` with `juce_add_binary_data` into the
`gmosh::WebAssets` namespace and passes `NEEDS_WEB_BROWSER TRUE` to
`juce_add_plugin`, which both defines `JUCE_WEB_BROWSER=1` and links the
platform web view (WebKitGTK via pkg-config on Linux). There is no web server
and no data folder: the page is served from the binary by the resource
provider at `juce://juce.backend/`.

## The boundary

The page owns presentation and interaction. The engine owns every fact:

* **Parameters** travel as normalised 0..1. `ui.controls` describes the six the
  page may edit (id, label, min, max) straight from the APVTS, so the ranges
  exist in exactly one place. Branch lengths and the wheel ride in `ui.state`
  instead because they belong to the plant, not the control strip.
* **The plant** is grown by `PlantGeometry` in unit space and sent as strokes +
  the reach they measure. The page scales them to the ring, exactly as the old
  native view did, so the on screen plant cannot drift from the plant the audio
  thread resolves, and a branch of length *x* reaches the handle that length
  asks for at any window size.
* **Keys**: the page forwards the character it received; `ComputerKeyboard` does
  the mapping and the octave shift. Nothing about the keyboard map is duplicated
  in JS.

### Protocol

```
C++ -> JS   ui.controls {controls:[{id,label,min,max}]}
          ui.state    {params{..}, lengths[..], wheel, wheelScale, active, seedId, octave}
          ui.plant    {seedId, branches:[{hue,sat,bri,reach,strokes:[[x0,y0,cx,cy,x1,y1,depth]]}]}
          ui.genes    {groups:[name], genes:[{i,label,g,v}]}
          ui.keys     {hint}
JS  -> C++  ui.ready {}
          ui.param {id, value}        normalised
          ui.gene {index, value}      normalised
          ui.note {note, velocity}    velocity 0 = note off
          ui.newSeed {} / ui.plantBranch {branch} / ui.requestGenes {}
          ui.key {ch, down}
          ui.error {message}          window.onerror -> the JUCE log
```

Two rules that took a debugging session to learn:

* `juce::var(root)` **owns** the `DynamicObject`. Wrapping the same pointer
  twice frees it in the first temporary and the second use is a dangling
  pointer (it segfaults inside `JSON::writeToStream`).
* Anything pushed before the page has installed its listeners is lost. The
  back end therefore sends nothing until `ui.ready`, and resets its change
  detection at that moment; the 30 Hz publisher otherwise compares against a
  signature the page never received.

## Animation (plant.js)

The page never re-decides the plant's shape; it deforms what it was given:

* **wind** — a bend that grows with distance from the seed (`r^1.6`), the sum
  of three incommensurate sines, phase offset by the golden angle per branch,
  so no two branches move together. Twigs add a small extra flutter.
* **touch** — grab, drag and release drive a damped spring in two channels
  (angle and length), which is what makes a released branch twang and settle
  instead of snapping.
* **sound** — a note pumps energy into its branch: the leaves sway harder and
  the branch glows, then decays.
* **growth** — the drawn length follows the parameter rather than jumping to
  it, so host automation and a fast drag both read as growing.

The renderer runs on `requestAnimationFrame` and stops when the document is
hidden or the gene page is open.

## Verifying it headless

The Xvfb recipe in `Implementation-Status.md` §7 still works; WebKitGTK needs
`WEBKIT_DISABLE_COMPOSITING_MODE=1` on this box. Text renders now (the old
"no fontconfig, no text" caveat was a native-rendering one).

Two measurements worth repeating:

* **The plant moves**: two screenshots one second apart must differ in the
  branch area (measured 219 pixels of a 900x700 frame with a 0.018 rad wind).
* **State arrives**: the readouts must show the defaults from
  `createParameterLayout` (volume 0.75, wheel scale 0.85, velocity 0.60), not
  the page's own 0.5 placeholders.

`WebUI/app.js` also has a no-backend path (`loadMockData`): opening `index.html`
in a plain browser draws a random plant and runs the animation, which is the
fastest way to tune the motion without launching the plugin.
