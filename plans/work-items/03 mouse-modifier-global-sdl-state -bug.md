# 03 mouse-modifier-global-sdl-state -bug

**Priority:** MEDIUM
**Type:** Bug
**Raised by:** Gemini, GPT
**Model:** claude-sonnet-4-6

---

## Problem

`libs/draxul-window/src/sdl_event_translator.cpp:32` samples Ctrl/Shift/Alt modifier state from the global SDL keyboard state (e.g., `SDL_GetModState()`) rather than from the modifier bits carried in the queued SDL mouse event being translated. Under delayed or bursty input, the global state can differ from the state at the time the event was enqueued. This causes wrong modifier flags for click, drag, and wheel events — Neovim receives incorrect mouse+modifier combinations.

---

## Fix Plan

- [ ] Read `libs/draxul-window/src/sdl_event_translator.cpp` in full.
- [ ] Identify all sites that call `SDL_GetModState()` during mouse event translation.
- [ ] Replace those calls with the modifier bits embedded in the event struct itself:
  - `SDL_MouseButtonEvent.mod` for button events (SDL3 carries this)
  - `SDL_MouseWheelEvent.mod` for wheel events
  - `SDL_MouseMotionEvent.mod` for motion/drag events
  - If SDL3 does not carry all modifier bits in the event, document which ones are unavoidably sampled globally and add a comment explaining the limitation.
- [ ] Build and run smoke test + ctest.
- [ ] Verify manually: hold Ctrl, click in Neovim, confirm `<C-LeftMouse>` is received correctly.

---

## Acceptance

- Mouse event modifier state matches the modifier state at event enqueue time, not the current global state.
- Ctrl+click, Shift+drag, and Ctrl+scroll all produce correct key modifier flags in Neovim.

---

## Interdependencies

- Validates via **09-test** (mouse-modifier-fidelity).
- No upstream dependencies.
