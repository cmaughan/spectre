# 18 configurable-terminal-colors -feature

**Priority:** LOW
**Type:** Feature (user-facing, high quality-of-life)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`shell_host_unix.cpp` (and likely its Windows equivalent) hardcodes foreground (`{0.92, 0.92, 0.92}`) and background (`{0.08, 0.09, 0.10}`) terminal colors. These are not read from `config.toml`. A user who changes their Neovim colorscheme or wants a light-mode terminal cannot match the Draxul shell pane to their preferences. This is a usability gap whenever Draxul is used as a general-purpose terminal.

---

## Implementation Plan

- [ ] Read `libs/draxul-host/src/shell_host_unix.cpp` (and/or Windows equivalent) to find where the hardcoded colors are used.
- [ ] Read `app/app_config.h` to understand the current config schema and TOML parsing approach.
- [ ] Add a new `[terminal]` section to `config.toml` with `fg` and `bg` color fields:
  ```toml
  [terminal]
  fg = "#eaeaea"
  bg = "#141516"
  ```
- [ ] Add the corresponding `TerminalConfig` struct to `AppConfig`.
- [ ] Wire the TOML parse for the new section.
- [ ] Plumb the resolved colors from `AppConfig` into the `ShellHost` constructor or `initialize()` call — do not pass them as a global; use the existing `Deps` pattern if `ShellHost` already has one.
- [ ] Update `config.toml` in the repo root with the new section and the current defaults.
- [ ] Build and run: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] Run `clang-format`.

---

## Acceptance

- `config.toml` `[terminal]` `fg`/`bg` values are reflected in the shell pane background and default text color.
- If the section is absent, the current hardcoded defaults are used (no regression).
- Hex color parsing handles `#RRGGBB` and `#RGB` formats (or reuses existing color parse helpers).

---

## Interdependencies

- `12-refactor` (app_config monolith split) — not a hard dependency but a cleaner split makes adding config sections easier.
- No upstream blockers.

---

*claude-sonnet-4-6*
