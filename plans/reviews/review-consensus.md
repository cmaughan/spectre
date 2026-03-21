All 24 work items created. Here's the full summary:

---

## Done

### `plans/reviews/review-consensus.md` — replaced
A proper synthesis review, not just a wrapper note. Structured as a meeting-room conversation covering:
- **5 unanimous agreements** (Megacity, unbounded RPC queue, strong tests, TerminalHostBase too big, CellText truncation)
- **Unique findings** by each reviewer (GPT: file-open injection, dual test harness; Gemini: error reporting inconsistency; Claude: TextServiceConfig duplication, mid-frame resize gap)
- **Disagreements** resolved (stale docs: low priority; TerminalHostBase before App.cpp)
- **Interdependency map** showing which items block which

### `plans/work-items/` — 24 items (00–23)

| Range | Type | Count |
|-------|------|-------|
| 00–04 | `-bug` | 5 |
| 05–13 | `-test` | 9 |
| 14–21 | `-refactor` | 8 |
| 22–23 | `-feature` | 2 |

**Key interdependency chains:**
- `04` (AppConfig SDL decoupling) → `18` (SdlWindow decomposition)
- `14` (Megacity removal) → renderer interface cleanup, unblocks GpuCell packing
- `17` (TerminalHostBase decomposition) → shell crash + scrollback resize tests
- `00 + 01` (file-open fixes) → `05` (special-chars test)
- `03` (RPC queue fix) → icebox `18 rpc-burst-transport` can be un-iceboxed

**Not created (already in icebox):** IME composition, live config reload, ANSI palette, background transparency, scrollback config, window state persistence, hierarchical config, native tab bar, command palette, font fallback inspector, performance HUD, RPC burst test, RPC codec fuzzing, startup rollback+clipboard test.
