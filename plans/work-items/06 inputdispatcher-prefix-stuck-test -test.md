# 06 inputdispatcher-prefix-stuck-test -test

**Priority:** MEDIUM
**Type:** Test (regression guard for chord-prefix bug fix)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

The chord prefix stuck-state bug (`01-bug`) has no automated regression test. Without a test, the fix can be silently reverted and the behavior will not be caught until a user reports it again.

---

## Implementation Plan

- [ ] Read `libs/draxul-nvim/src/input_dispatcher.cpp` and its header. Understand how to construct an `InputDispatcher` in a test and inject synthetic key events.
- [ ] Read existing input dispatcher tests (search `tests/` for dispatcher-related test files) to understand the test pattern.
- [ ] Write the following test cases:
  - [ ] **Normal prefix sequence** — prefix key-down, chord key-down → chord action fires; dispatcher returns to normal state.
  - [ ] **Prefix key abandoned (key-up only)** — prefix key-down, prefix key-up (no chord key) → dispatcher returns to normal state; next unrelated key is passed through as normal input, NOT consumed as a chord.
  - [ ] **Two consecutive prefix activations** — activate prefix, abandon it (key-up), activate again, complete a chord → second chord fires correctly.
- [ ] Add the tests to an existing input-related test file or create `tests/input_dispatcher_tests.cpp`.
- [ ] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- Tests pass after the `01-bug` fix is applied.
- Tests fail on the unfixed code (verifying they actually cover the bug).
- No production code changes required for this test item.

---

## Interdependencies

- `01-bug` (chord prefix stuck) — this test should be written together with or immediately after the bug fix.

---

*claude-sonnet-4-6*
