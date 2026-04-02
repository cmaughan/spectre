# WI 54 — grid-handle-null-init-test

**Type**: test  
**Priority**: 6 (regression guard for WI 48)  
**Source**: review-consensus.md §T3 [P]  
**Produced by**: claude-sonnet-4-6

---

## Problem / Goal

After WI 48 (vk-null-grid-handle-dereference) is fixed, automated tests must confirm that both `GridHostBase::initialize()` and `CommandPaletteHost::dispatch_action()` handle a null handle gracefully rather than crashing.

---

## Pre-condition

**WI 48 must be merged before writing these tests.**

---

## Tasks

- [ ] Read `libs/draxul-host/src/grid_host_base.cpp` and the `GridHostBase::initialize()` method — understand what a null-handle failure result looks like after the WI 48 fix.
- [ ] Read `app/command_palette_host.cpp` and the `dispatch_action()` method — understand the null-handle early-return path.
- [ ] Read `tests/support/fake_renderer.h` — determine whether the fake renderer's `create_grid_handle()` can be configured to return `nullptr`.
- [ ] If the fake renderer cannot return null: add a constructor option or a `set_fail_grid_handle(true)` method to `FakeRenderer` (or a new `NullHandleRenderer` test double) that makes `create_grid_handle()` return `nullptr`.
- [ ] Write test: `GridHostBase::initialize()` with a null-returning renderer returns a failure result and does not dereference the handle.
- [ ] Write test: `CommandPaletteHost::dispatch_action()` with a null-returning renderer logs an error and returns early without crashing.
- [ ] Build and run: `cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`

---

## Acceptance Criteria

- At least 2 new test cases.
- Both tests pass.
- No existing test regressions.

---

## Interdependencies

- **Requires WI 48** merged first.
- Modifying `FakeRenderer` to support null-handle mode should be backward-compatible; all existing tests using `FakeRenderer` must still pass.

---

## Notes for Agent

- Prefer extending the existing fake rather than creating a separate renderer double, to keep the test infrastructure lean.
- Do not write tests that require a real GPU or Vulkan driver; use the fake renderer throughout.
