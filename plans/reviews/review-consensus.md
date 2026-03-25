All 34 files written. Here's the summary:

---

## What was produced

**`plans/reviews/review-consensus.md`** — full synthesis document covering:
- How the three reviewers (Claude/GPT/Gemini) agreed, disagreed, and nuanced each other
- A table of strong agreements and a disagreements section with resolutions
- Stale/already-fixed issues called out explicitly
- Recommended fix order across all 33 items
- An interdependency table with sub-agent suggestions

**`plans/work-items/` — 33 new work items:**

| Range | Category | Count |
|---|---|---|
| `00–02` | Bugs | 3 |
| `03–15` | Tests | 13 |
| `16–23` | Refactors | 8 |
| `24–32` | Features | 9 |

**Key interdependencies flagged:**
- `00` (CWD fix) → `12` (CLI test validates the fix)
- `02` (ligature alloc fix) → `09` (bounds test exercises the fix)
- `07` (font cascade test) → `26` (per-pane font size feature needs that test first)
- `17` (IRenderer cleanup) → `20` (render-test capture separation) — do in sequence, same agent
- `16` (app-support split) ↔ `23` (UiRequestWorker evaluate) — coordinate scope
- `25` (session restore) → check icebox `36` (window-state-persistence) for overlap
- `08` (SplitTree min-size test) → `29` (pane drag-reorder depends on that ratio logic)

Items already in the icebox were not re-created (26 excluded items listed in the consensus doc).
