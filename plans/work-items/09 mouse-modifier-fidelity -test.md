# 09 mouse-modifier-fidelity -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Gemini (validates 03-bug fix)
**Model:** claude-sonnet-4-6

---

## Problem

Mouse modifier state is currently sampled from global SDL state rather than the event itself. There is no test that constructs synthetic SDL mouse events with specific modifier flags and asserts the translated Neovim mouse string carries the correct modifiers.

---

## Implementation Plan

- [ ] Read `libs/draxul-window/src/sdl_event_translator.cpp` and the corresponding header to understand the translation API.
- [ ] Find or create a test-seam to feed synthetic `SDL_MouseButtonEvent`, `SDL_MouseWheelEvent`, `SDL_MouseMotionEvent` into the translator without a real SDL window.
- [ ] Write tests in `tests/mouse_modifier_tests.cpp`:
  - Construct a `SDL_MouseButtonEvent` with `mod = KMOD_CTRL | KMOD_SHIFT` and button `SDL_BUTTON_LEFT`, translate it, assert the output string is `<C-S-LeftMouse>` (or equivalent Neovim notation).
  - Construct a wheel event with `mod = KMOD_ALT`, assert `<A-ScrollWheelUp>`.
  - Construct a drag/motion event with Shift, assert correct modifier prefix.
  - Test with no modifiers as a baseline.
- [ ] Add to `tests/CMakeLists.txt`.
- [ ] Run ctest.

---

## Acceptance

- Synthetic mouse events with modifier flags produce the correct Neovim mouse notation.
- No global SDL state is consulted during the test (event struct values are authoritative).

---

## Interdependencies

- Best written after **03-bug** fix (global-SDL-state removal). Can be TDD — test will fail before fix if global state is inconsistent in tests.
