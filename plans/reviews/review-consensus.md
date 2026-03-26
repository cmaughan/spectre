All 25 new work items are in place alongside the existing `24 osc52-clipboard-read`. Here's a summary of what was produced:

---

## What was produced

### `plans/reviews/review-consensus.md` — full synthesis

- **10 strong agreements** across all three reviewers (God Object `App`, untestable `run_render_test`, `void*` render context, MegaCity structural reach, error idiom inconsistency, `FT_Face` lifetime hazard, chord prefix stuck-state, `MegaCityCodeConfig` in core config, `Grid::scroll()` complexity, thin concurrency/persistence tests)
- **Agent-specific findings** attributed by reviewer with `[C]`, `[G]`, `[P]` tags
- **Disagreement table** with resolutions (error unification, App decomposition scope, MegaCity priority, `void*` strategy)
- **Stale/already-fixed** issues called out explicitly (23 items already in complete or icebox, not re-planned)
- **Recommended fix order** across all tiers
- **Interdependency map** with sub-agent recommendations

### 25 new work items

| Range | Type | Count |
|---|---|---|
| `00–03` | Bugs | 4 |
| `04–12` | Tests | 9 |
| `13–19` | Refactors | 7 |
| `20–24` | Features | 5 |

### Key interdependencies flagged

- `00` (FT_Face bug) → `11` (fontresolver test: add regression)
- `01` (grid overflow) → `05` (grid OOB test covers it)
- `02` (APPDATA bug) → `07` (config test covers it)
- **`04 + 13`** = same agent pass (render-test refactor + test)
- **`08 + 15`** = same agent pass (attr-cache refactor + test)
- **`05 + 17`** = same agent pass (scroll split + OOB test)
- **`09 + 10 + 12`** = same agent pass (MegaCity stability tests)
- **`11 + 18`** = same agent pass (fontresolver extraction + test)
- `19` (void* context) — coordinate scope with icebox `25 renderer-backend-parity-cleanup`
- `20` (OSC 8) → `24` (OSC 133) share dispatch infrastructure; do OSC 8 first
