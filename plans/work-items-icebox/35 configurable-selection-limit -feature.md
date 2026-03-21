# Configurable Selection Limit

**Type:** feature
**Priority:** 35
**Raised by:** Claude

## Summary

After work item `04` raises `kSelectionMaxCells` to a reasonable default, expose it as `[terminal] selection_max_cells` in `config.toml` so users can tune the maximum selection size if needed.

## Background

Work item `04` fixes the immediate bug (256 cells is dangerously small). This feature item makes the limit configurable for users with specific needs: a very large selection limit for users who regularly copy large output, or a smaller limit on constrained systems. The implementation mirrors work item `34` (configurable scrollback capacity).

## Implementation Plan

### Files to modify
- `libs/draxul-host/src/terminal_host_base.h` — change `kSelectionMaxCells` from a compile-time constant to a runtime parameter
- `libs/draxul-host/src/terminal_host_base.cpp` — use the runtime parameter
- `libs/draxul-app-support/` — extend `AppConfig` with `selection_max_cells` field (default 8192 after work item `04`)
- Config parsing — parse `[terminal] selection_max_cells` from `config.toml`
- `app/app.cpp` — pass `config.selection_max_cells` to `TerminalHostBase`

### Steps
- [ ] Add `int selection_max_cells = 8192` to `AppConfig` (or whatever the new default from work item `04` is)
- [ ] Parse `[terminal] selection_max_cells` from config; clamp to a sane range (e.g., 256–1048576)
- [ ] Change `TerminalHostBase` to accept the selection limit as a parameter
- [ ] Update all construction sites
- [ ] Document the config option
- [ ] Verify work item `15` tests still pass with the configurable limit

## Depends On
- `04 selection-silent-truncation -bug.md` — must fix the default first

## Blocks
- None

## Notes
The selection limit is simpler to change at runtime than the scrollback capacity (it just affects the next selection, not existing buffered data). However, startup-only is sufficient and simpler.

> Work item produced by: claude-sonnet-4-6
