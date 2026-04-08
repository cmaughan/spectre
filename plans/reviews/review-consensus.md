Done. Here's a summary of what was produced:

---

## `review-consensus.md` — Replaced

A full synthesis covering:

**Unanimous (all 3 agents):** App god object, no drain budget, atlas upload ambiguity, overlay hardwiring, capability-via-debug-string. All already tracked.

**Two-agent agreements:** VT parser gaps, config dep leakage, SplitTree O(n), duplicate NvimHost handler, Metal header hazard, wire_gui_actions monolith. All already tracked.

**Agent-specific new findings:**
- Claude: workspace tab focus preservation test, alt-screen round-trip test *(new)*
- Gemini: ChromeHost hardcoded Catppuccin colors *(new)*
- GPT: NvimHost clipboard eager polling every frame *(new)*

**Disagreements:** MegaCity isolate vs remove (consensus: follow WI 133 plugin-boundary first), ChromeHost layout engine (consensus: no, add tests instead), SDL coupling (consensus: IWindow abstraction is sufficient).

---

## 4 New Work Items

| File | Type | Summary |
|---|---|---|
| `00 workspace-tab-focus-preservation -test.md` | test | Verify focus is preserved/restored when switching workspace tabs, using fake deps — no Neovim required |
| `01 alt-screen-roundtrip-fidelity -test.md` | test | Verify main-screen content survives ESC[?1049h/l round-trip, including resize-during-alt-screen variant |
| `02 nvimhost-clipboard-eager-polling -refactor.md` | refactor | Replace per-frame `clipboard_text()` poll in `NvimHost::pump()` with SDL3 clipboard-update event subscription |
| `03 chromehost-hardcoded-theme-colors -feature.md` | feature | Expose `[chrome]` config section so tab bar, pills, dividers, and status bar colors are user-configurable |

**Key interdependencies flagged in consensus:** Phase chains `115→126→127`, `116→122`, `119+120+121→125→132`, and the new items `134/135` benefit from WI 125 being done first; `137` should coordinate with WI 128.
