Everything is in place. Note there's a pre-existing `05 windows-cmdline-quoting -bug.md` that coexists with the new `05 grid-scroll-per-direction-tests -test.md` — they're different files, both starting with `05`. The numbers are just sorting hints; the filenames are unambiguous.

Here's what was produced:

---

## Consensus file (`plans/reviews/review-consensus.md`)

Synthesized from **Claude** (substantial review) and **GPT** (substantial review). Gemini's file was empty this round. The consensus document:
- Calls out unanimous agreements on what's good and what needs fixing
- Attributes unique findings to each agent
- Resolves the two minor disagreements (BMP replacement, `startup_resize_state.h` simplification — both deferred)
- Includes a dependency graph and parallelism streams

---

## 23 new work items (`plans/work-items/00–22`)

| Range | Type | Items |
|---|---|---|
| 00–02 | `-bug` | multi-pane timing, chord prefix stuck, grid_line replay boundary |
| 03–10 | `-test` | HostManager lifecycle, app smoke, grid scroll directions, prefix-stuck regression, grid_line boundary, highlight reverse-video, CellText truncation, wide-char scroll scenario |
| 11–17 | `-refactor` | pump_once decomposition, app_config split, TextInputEvent safety, for_each_host template, GuiActionHandler dispatch map, SplitTree const, log_would_emit atomic |
| 18–22 | `-feature` | configurable terminal colors, OSC 7 CWD, searchable scrollback, per-pane env overrides, bracketed paste confirmation |

**Not created** (already in icebox): live-config-reload, window-state-persistence, DPI scaling, URL detection, ANSI palette, scrollback/selection limits, performance HUD, native tab bar, command palette, font fallback inspector, MegaCity removal, agent scripts dedup, diagnostics panel tests, HostManager dynamic_cast removal, and others.

**Key interdependency chains:**
- `00-bug` → validates better once `03-test` exists
- `01-bug` ↔ `06-test` (write together)
- `02-bug` ↔ `07-test` (write together)
- `12-refactor` (app_config split) unblocks icebox items 56 and 37
- `11-refactor` (pump_once) enables `04-test` (app smoke test)
