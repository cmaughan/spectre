# Startup Failure Rollback and Clipboard-Provider Failure Test

**Type:** test
**Priority:** 22
**Raised by:** GPT

## Summary

Add tests that verify `App::initialize()` correctly cleans up partially-initialised state when a subsystem fails mid-startup (renderer init failure after window creation, font load failure after renderer init) and that clipboard-provider failure during nvim init is handled gracefully without a crash.

## Background

`App::initialize()` initialises subsystems in sequence. If a later subsystem fails, all earlier subsystems must be cleaned up correctly to avoid resource leaks or use-after-free on process exit. Work item `07` adds error dialogs for these cases; this test item verifies that the cleanup itself is correct and that the process exits cleanly in each failure scenario.

The clipboard-provider failure during nvim init is a separate path: if the clipboard provider cannot be initialised (e.g., on a headless system with no clipboard daemon), nvim initialisation should fail gracefully rather than crashing.

## Implementation Plan

### Files to modify
- `app/tests/` — add `startup_rollback_test.cpp`
- Relevant `CMakeLists.txt` — register with ctest

### Steps
- [ ] Write test: window creation succeeds, renderer init fails → window is destroyed on `App` destruction, no leak — skipped (App not in linkable library; requires work item 31)
- [ ] Write test: window and renderer succeed, font load fails → renderer and window destroyed correctly — skipped (same reason)
- [ ] Write test: all init steps succeed, clipboard provider fails during nvim init → process exits cleanly, no crash — skipped (same reason)
- [x] Write test: `App::initialize()` returns an error code for each failure case; verify the code matches the failed subsystem — documented expected error strings; runtime verification skipped pending work item 31
- [ ] Where possible, use mock implementations of `IRenderer` / `IWindow` that can be forced to fail — skipped (requires work item 31)
- [x] Register with ctest

## Depends On
- `07 init-failure-no-dialog -bug.md` — the dialog fix should be in place, but these tests focus on cleanup, not the dialog

## Blocks
- None

## Notes
The tests may require refactoring `App::initialize()` to accept injected mock subsystems (dependency injection). If the constructor is too tightly coupled to concrete types, consider extracting an `AppInitialiser` or factory that can be tested in isolation. This is a secondary benefit of work item `31` (App decomposition).

> Work item produced by: claude-sonnet-4-6
