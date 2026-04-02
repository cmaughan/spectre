# WI 56 — library-boundary-renderer-includes

**Type**: refactor  
**Priority**: 8 (library boundary porousness makes private renderer refactors expensive)  
**Source**: review-consensus.md §R2 [P][C][G]  
**Produced by**: claude-sonnet-4-6

---

## Problem

Two library boundary violations exist that expose renderer-private headers to consumers who should only see the public API:

1. **`draxul-ui`**: `libs/draxul-ui/src/ui_panel.cpp` reaches into renderer headers via a relative path. The `draxul-ui` CMakeLists.txt does not declare a dependency on `draxul-renderer`, making this a hidden link-order dependency.

2. **`draxul-megacity`**: `libs/draxul-megacity/CMakeLists.txt:80` explicitly adds renderer-private backend directories to the include path. `megacity_render_vk.cpp` and `megacity_render.mm` include private render-context headers directly. Any refactor of private renderer internals requires coordinated changes in MegaCity.

These violations mean:
- A private renderer header rename silently breaks a dependent library.
- CI does not catch missing `target_link_libraries` because the relative-path include bypasses CMake's dependency tracking.

---

## Tasks

- [ ] Read `libs/draxul-ui/src/ui_panel.cpp` — identify exactly which renderer header(s) are included via relative path and why.
- [ ] Read `libs/draxul-ui/CMakeLists.txt` — confirm `draxul-renderer` is not in `target_link_libraries`.
- [ ] Determine whether the `draxul-ui` dependency on the renderer is necessary: can the needed types come from `draxul-types` or through a forward declaration? If yes, remove the include entirely. If no, add `draxul-renderer` as an explicit dependency in the CMakeLists.
- [ ] Replace the relative-path include in `ui_panel.cpp` with an angle-bracket include of the public header (`#include <draxul/renderer.h>` or similar).
- [ ] Read `libs/draxul-megacity/CMakeLists.txt` around line 80 — identify all private renderer directories added to the include path.
- [ ] Read `libs/draxul-megacity/src/megacity_render_vk.cpp` and `megacity_render.mm` — identify which private renderer headers they include and whether those APIs belong in the public `draxul-renderer` interface.
- [ ] For each private header used by MegaCity: either (a) promote the needed declaration to the public `draxul-renderer` interface, or (b) move the MegaCity code that needs it into the renderer library as a registered `IRenderPass`. Do not copy private headers.
- [ ] Remove the private-directory `include_directories` from `libs/draxul-megacity/CMakeLists.txt`.
- [ ] Full build both platforms or at minimum the active platform: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- `draxul-ui` CMakeLists.txt declares its actual dependencies; no relative-path renderer includes remain.
- `draxul-megacity` CMakeLists.txt does not add renderer-private directories to the include path.
- All existing tests pass.
- Smoke test passes.

---

## Interdependencies

- Independent of WI 48–55 and WI 57–60.
- **High blast radius**: touches CMakeLists across two libraries and their source files. Full build verification required.
- A sub-agent is appropriate here due to the breadth of files to read before making changes.

---

## Notes for Agent

- Read all relevant files first; do not guess which headers are included.
- Prefer promoting types to the public interface over copying headers.
- If promoting a private renderer type requires significant API design work, scope the fix to `draxul-ui` only and file a separate item for `draxul-megacity`'s renderer coupling.
- After the fix, verify with `cmake --build build 2>&1 | grep -i "warning\|error"` that no new include-path warnings appear.
