# WI 53 — overlay-input-routing-tests

**Type**: test  
**Priority**: 5 (regression guard for WI 50)  
**Source**: review-consensus.md §T2 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Problem / Goal

After WI 50 (overlay-input-incomplete-interception) is fixed, automated tests must prevent the leaky event routing from regressing. This item adds focused `InputDispatcher` tests for all four leaked paths.

---

## Pre-condition

**WI 50 must be merged before writing these tests.**

---

## Tasks

- [ ] Read `app/input_dispatcher.cpp` and `app/input_dispatcher.h` to understand how `overlay_host_` is set, cleared, and checked in each event path after the WI 50 fix.
- [ ] Read existing `tests/` for `InputDispatcher` tests (search for `input_dispatcher`) to find the correct test file to extend.
- [ ] Write test: When an overlay host is active, a `MouseButtonEvent` is **not** forwarded to the underlying pane host.
- [ ] Write test: When an overlay host is active, a `MouseWheelEvent` is **not** forwarded to the underlying pane host.
- [ ] Write test: When an overlay host is active, a `TextEditingEvent` goes to the overlay host, **not** the underlying host.
- [ ] Write test: When the overlay is cleared (`overlay_host_ = nullptr`), all event types reach the pane host again.
- [ ] Write test: When an overlay is active, mouse events are still forwarded to the **overlay** host itself (not silently dropped).
- [ ] Build and run: `cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`

---

## Acceptance Criteria

- At least 5 new test cases covering the above scenarios.
- All tests pass.
- No existing test regressions.

---

## Interdependencies

- **Requires WI 50** merged first.
- No other dependencies.

---

## Notes for Agent

- Use the existing fake host / fake renderer infrastructure from `tests/support/`.
- Tests should exercise `InputDispatcher` directly; no need to spin up a full App.
- The overlay host in tests can be a minimal `IHost` stub that records received events.
