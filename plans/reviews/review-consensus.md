All done. Here's the summary:

---

## What was produced

### `plans/reviews/review-consensus.md` — replaced

A proper synthesis, structured as a room conversation:
- **Unanimous agreements** (3 agents): startup rollback saves config, app.cpp still too concentrated, test seams exist, module boundaries almost clean, dispatch_action resolved
- **Unique findings** per agent: Claude (grid bounds, vtparser, release UB, cmd-line quoting), Gemini (mouse modifier global state, clipboard test drift, duplicate planning items), GPT (forceful close, unconditional clipboard provider install)
- **Disagreements resolved**: void* Metal bridge (complete), GridHostBase decomp (complete), dynamic_cast (new item created)
- **Interdependency map** with parallel stream suggestions
- Attributed by model throughout

### `plans/work-items/` — 22 items (00–21)

| Range | Type | Count |
|---|---|---|
| 00–05 | `-bug` | 6 |
| 06–14 | `-test` | 9 |
| 15–18 | `-refactor` | 4 |
| 19–21 | `-feature` | 3 |

**Key interdependency chains:**
- `00-bug` → `06-test` + `14-test` (startup config save chain)
- `01-bug` → `07-test` (grid bounds chain)
- `02-bug` → `08-test` (vtparser chain)
- `03-bug` → `09-test` (mouse modifier chain)
- `15-refactor` (SDL decoupling) → unblocks `19-feature` (DPI scaling) cleanly

**Not created (already in icebox):** live-config-reload, command-palette, performance-hud, IME composition, native tab bar, remote nvim, configuration GUI, font fallback inspector, window state persistence, hierarchical config, configurable ANSI palette, scrollback/selection limits, background transparency, bless-render-test-flag-ci, Megacity removal, agent-scripts deduplication, guicursor full support, URL detection, window title from nvim.
