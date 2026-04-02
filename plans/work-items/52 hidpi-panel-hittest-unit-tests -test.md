# WI 52 — hidpi-panel-hittest-unit-tests

**Type**: test  
**Priority**: 4 (regression guard for WI 49)  
**Source**: review-consensus.md §T1 [P][G][C]  
**Produced by**: claude-sonnet-4-6

---

## Problem / Goal

After WI 49 (hidpi-panel-hittest-double-scale) is fixed, there is no automated guard preventing the double-scale regression from returning. This item adds focused unit tests for the coordinate contract.

---

## Pre-condition

**WI 49 must be merged before writing these tests**, so the tests validate the correct post-fix behaviour rather than the broken pre-fix state.

---

## Tasks

- [ ] Read `libs/draxul-ui/include/draxul/ui_panel.h` and `libs/draxul-ui/src/ui_panel.cpp` — understand the complete `PanelLayout` struct and `contains_panel_point()` signature.
- [ ] Read existing `tests/` for any panel or UI panel tests (search for `ui_panel` or `PanelLayout`) to pick the right test file to extend or create alongside.
- [ ] Write a test: `PanelLayout` at `pixel_scale=1.0` — confirm that a point inside the panel's logical rect returns true and a point outside returns false.
- [ ] Write a test: `PanelLayout` at `pixel_scale=2.0` — confirm the same boundary behaviour with physical (doubled) coordinates; this is the regression test for WI 49.
- [ ] Write an integration-style test (using the fake-renderer / fake-window infrastructure): a visible diagnostics panel correctly blocks host mouse input for events whose physical coordinates land on the panel on a 2× display.
- [ ] Build and run: `cmake --build build --target draxul-tests && ctest --test-dir build -R draxul-tests`

---

## Acceptance Criteria

- At least 3 new test cases covering 1× and 2× pixel scale boundary conditions.
- All tests pass on `mac-debug` preset.
- No existing test regressions.

---

## Interdependencies

- **Requires WI 49** merged first.
- No other dependencies.

---

## Notes for Agent

- Use the existing `tests/support/fake_renderer.h` and `tests/support/fake_grid_pipeline_renderer.h` as the injection model.
- Keep tests in the `tests/` directory following existing naming conventions (`*_tests.cpp`).
- Do not test internal ImGui layout logic; test only the `PanelLayout` coordinate contract.
