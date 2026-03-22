# 11 keybinding-conflict-detection -test

**Priority:** MEDIUM
**Type:** Test
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

If a user configures two actions to the same key in `config.toml`, the current behaviour is undefined: silent last-wins, or potentially both actions firing. There is no test for this case, and no documented contract about what should happen.

---

## Implementation Plan

- [ ] Read `libs/draxul-app-support/include/draxul/app_config.h` and the keybinding parsing code.
- [ ] Read `libs/draxul-app-support/src/` for keybinding load/apply logic.
- [ ] Decide on the correct conflict policy (last-wins with WARN is simplest and user-friendly):
  - If the policy is not yet implemented, implement it.
  - If it is implemented, just write the test.
- [ ] Write `tests/keybinding_tests.cpp` (or add to existing):
  - Construct an `AppConfig` with two bindings for the same key (e.g., both `toggle_diagnostics` and `copy` bound to `Ctrl+D`).
  - Load the config.
  - Assert: either a WARN is logged about the conflict, or the last binding wins deterministically (no crash, no silent double-action).
  - Assert: the first key still dispatches the correct (winning) action.
- [ ] Add to `tests/CMakeLists.txt`.
- [ ] Run ctest.

---

## Acceptance

- Duplicate keybinding in config: clear WARN logged, deterministic resolution (no crash, no silent failure).
- Normal (non-conflicting) keybindings continue to work.

---

## Interdependencies

- No upstream dependencies.
- Relates to the `dispatch-gui-action-map` refactor (complete) — the map-based dispatch should make conflict detection straightforward.
