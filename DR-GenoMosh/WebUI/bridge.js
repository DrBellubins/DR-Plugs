// The bridge to the C++ editor.
//
// Plain JS on purpose: JUCE injects window.__JUCE__ into every page it loads
// (see juce_WebBrowserComponent.cpp), and the event API is all this synth needs.
// No build step, no npm package: the files in this folder are the source of
// truth and are embedded in the plugin binary by CMake.
//
//   C++ -> JS : emitEventIfBrowserIsVisible(id, value)  -> onEvent(id, fn)
//   JS  -> C++: sendEvent(id, value)                    -> Options::withEventListener(id, fn)
//
// Without a backend (opening this file in a browser) the calls are no-ops, which
// lets the plant be developed and eyeballed outside the plugin.

const backend = (typeof window !== "undefined" && window.__JUCE__ && window.__JUCE__.backend)
  ? window.__JUCE__.backend
  : null;

export const hasBackend = backend !== null;

export function onEvent(eventId, handler) {
  if (!backend) return;
  backend.addEventListener(eventId, (payload) => handler(payload));
}

export function sendEvent(eventId, payload) {
  if (!backend) return;
  backend.emitEvent(eventId, payload === undefined ? {} : payload);
}
