# WI 59 — copy-on-select

**Type**: feature  
**Priority**: 11  
**Source**: review-consensus.md §F3 [P][G]  
**Produced by**: claude-sonnet-4-6

---

## Goal

Add an optional `copy_on_select` configuration key. When enabled, completing a mouse selection in a terminal pane automatically copies the selected text to the system clipboard — matching the behaviour of xterm, gnome-terminal, and most Unix terminal emulators.

---

## Background

Currently selection is performed by click-drag and text is copied only when the user explicitly presses `Ctrl+Shift+C`. `copy_on_select` is a common expectation for terminal users migrating from Unix terminals.

---

## Tasks

- [ ] Read `libs/draxul-config/src/app_config_io.cpp` — find the terminal config section (`[terminal]`) and understand how to add a new boolean key.
- [ ] Add `copy_on_select = false` (default off, to preserve current behaviour) to the `AppConfig` / terminal config struct and its parser/serialiser.
- [ ] Read `app/input_dispatcher.cpp` (or whichever file handles the end of a mouse-drag selection) — find where selection completion is detected and clipboard copy is currently triggered for explicit Ctrl+Shift+C.
- [ ] When selection drag ends (mouse-button-up after drag), if `copy_on_select` is enabled, call the same clipboard copy path used by the explicit copy action.
- [ ] Add `copy_on_select` to `docs/features.md` under the Terminal Emulation section and the Configuration table.
- [ ] Build and run smoke test: `cmake --build build --target draxul draxul-tests && py do.py smoke`

---

## Acceptance Criteria

- `copy_on_select = false` (default): existing behaviour unchanged.
- `copy_on_select = true`: completing a drag selection copies text to clipboard automatically.
- Config key round-trips cleanly through save/load.
- `docs/features.md` updated.
- Smoke test passes.

---

## Interdependencies

- **WI 60** (double-triple-click-selection) modifies the same selection code; doing both in the same session is efficient.
- Independent of WI 48–57.

---

## Notes for Agent

- Default must be `false` to avoid surprising existing users.
- The clipboard write path already exists (used by Ctrl+Shift+C); reuse it, do not duplicate it.
- Do not add copy-on-select to the `IHost` interface; implement it entirely in the `InputDispatcher` or App layer where selection completion is detected.
