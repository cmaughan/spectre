# Refactor: Decouple draxul-config from Renderer/Window/Font

**Type:** refactor
**Priority:** 14
**Source:** Gemini review

## Problem

`libs/draxul-config/` is supposed to be a low-level, dependency-free config layer, but it currently:

1. **Publicly links** `draxul-renderer`, `draxul-window`, and `draxul-font` in its `CMakeLists.txt`.
2. **Includes SDL and `TextService`** headers from `app_config_io.cpp` — subsystem headers that have no business in a config I/O file.
3. **Carries `MegaCityCodeConfig`** in the core `app_config_types.h`, mixing optional-demo config into the shared type header.

This means any code that depends on config also transitively pulls in renderer, window, font, and (optionally) MegaCity headers, blowing up include times and creating non-obvious coupling.

## Investigation steps

- [ ] Read `libs/draxul-config/CMakeLists.txt` — list all public and private link deps.
- [ ] Read `libs/draxul-config/include/draxul/app_config_types.h` — identify which fields belong to core vs. optional modules.
- [ ] Read `libs/draxul-config/src/app_config_io.cpp` — find every subsystem include and why it is there.
- [ ] Check whether `MegaCityCodeConfig` is used outside the MegaCity library.

## Implementation steps

- [ ] **Remove subsystem links from `draxul-config/CMakeLists.txt`**: make renderer/window/font `PRIVATE` or remove them entirely. Any code that needs both config and renderer should link both in the consuming CMake target.
- [ ] **Move `MegaCityCodeConfig`** out of `app_config_types.h`. Options:
  - Move to `libs/draxul-megacity/include/draxul/megacity_config.h` (cleanest).
  - Guard with `#ifdef DRAXUL_ENABLE_MEGACITY` at minimum.
- [ ] **Remove SDL/TextService includes from `app_config_io.cpp`**: find what they are used for and either inline the minimal type needed or restructure the code.
- [ ] Build the project after each step and fix broken includes.
- [ ] Run `cmake --build build --target draxul draxul-tests` to confirm nothing broke.

## Acceptance criteria

- [ ] `draxul-config/CMakeLists.txt` no longer publicly links renderer, window, or font.
- [ ] `app_config_types.h` does not include any renderer, window, font, or SDL headers.
- [ ] `MegaCityCodeConfig` lives in the megacity library, not the core config header.
- [ ] Project builds clean on both macOS and Windows presets.
- [ ] No regressions in existing tests.

## Interdependencies

- **`02 appdata-empty-string-config -bug`**: fix bug `02` first (touches same file); then decouple.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
