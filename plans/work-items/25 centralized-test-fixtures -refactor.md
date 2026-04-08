# WI 25 — Centralised test fixtures (FakeWindow, FakeRenderer, FakeHost)

**Type:** refactor  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 6

---

## Goal

Create a shared test-support library containing canonical `FakeWindow`, `FakeRenderer`, `FakeHost`, `FakeGridHandle`, and `FakeClock` implementations so that new tests (WI 14-23 and beyond) do not each reinvent their own mocks.

---

## Problem

Claude's review noted that test fixtures are scattered — each test file reinvents mocks for `IWindow`, `IGridRenderer`, `IHost`, etc. Consequences:
- Adding a new interface method requires updating every independent mock.
- Inconsistent mock behaviour makes test failures ambiguous.
- New-host work (MegaCity, ChromeHost variants) requires understanding many mock patterns before writing a single test line.

`replay_fixture.h` is already a good example of a shared fixture; this extends the pattern to the renderer/window/host layer.

---

## Implementation Plan

- [ ] Audit existing test files for mock implementations of `IWindow`, `IGridRenderer`, `IHost`, `IGridHandle`, and any time/clock abstractions.
- [ ] Extract the canonical (most complete) version of each mock into a new file in `tests/support/` (which already contains `replay_fixture.h`).
  - `tests/support/fake_window.h`
  - `tests/support/fake_renderer.h`
  - `tests/support/fake_host.h`
  - `tests/support/fake_grid_handle.h`
  - `tests/support/fake_clock.h`
- [ ] Each fake should expose:
  - A constructor accepting optional overrides for specific behaviours (e.g. `create_grid_handle` returning null).
  - Call-count / argument-capture members for assertion.
- [ ] Update existing tests to use the shared fakes rather than their local copies (do this incrementally — at minimum the tests being added by WI 14-23 should use the shared fakes from day one).
- [ ] Wire the new support files into `CMakeLists.txt` as part of the `draxul-tests` target.

---

## Notes for the agent

- This is primarily a reorganisation task; no new test logic is being introduced.
- The test harness changes here will immediately pay dividends for WI 14, 16, 18, 19, 20, 22, and 23.
- Consider doing this as a prerequisite or early parallel task before the Wave 5 tests.

---

## Interdependencies

- Enables WI 14, 16, 17, 18, 19, 20, 22, 23 — they all need these fixtures.
- WI 22 (inputdispatcher-null-deps) needs `FakeGridHandle` with a null-return mode.
- WI 19 (toasthost-init-failure) needs `FakeGridHandle` that injects null.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
