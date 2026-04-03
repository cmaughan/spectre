---
# WI 81 — Raise Selection Cell Limit

**Type:** feature  
**Priority:** medium (complement to WI 62 which adds the warning)  
**Raised by:** [C] Claude, [G] Gemini, [P] GPT — unanimous  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`SelectionManager::kSelectionMaxCells = 8192` is a hard compile-time limit on selected cells. On a 200-column terminal, this is only ~40 rows. Users who select code blocks across function boundaries (common in Neovim workflows) hit this limit constantly. WI 62 adds a warning; this item raises or removes the limit.

---

## Investigation Steps

- [ ] Read `SelectionManager` to understand why the limit was introduced (memory, performance, or a specific bug)
- [ ] Check whether `kSelectionMaxCells` is used elsewhere (rendering overlay, clipboard copy)
- [ ] Estimate memory cost at higher limits: 8192 cells × 2 bytes (col, row) = 16 KB. At 65536 cells: 128 KB — still trivial
- [ ] Check if the clipboard copy path has a separate limit

---

## Proposed Change

- Raise `kSelectionMaxCells` from 8192 to 65536 (covers ~327 rows at 200 columns — sufficient for any realistic single-selection use case)
- If the limit was defensive (not for memory reasons), make it configurable: `selection_max_cells = 65536` in `[terminal]` config section with a comment about memory implications
- Alternatively, use a `std::vector` that grows dynamically up to a large configurable cap (removes the hard constant entirely)

---

## Implementation Steps

- [ ] Determine the reason for the 8192 limit (read git history or ask via comment)
- [ ] Raise `kSelectionMaxCells` to 65536 (or make it configurable)
- [ ] Update unit tests that use `kSelectionMaxCells` to use the new value
- [ ] Verify WI 62 warning callback still fires at the new limit threshold
- [ ] If making it configurable: add the config key, default to 65536, validate range (1024–524288)

---

## Acceptance Criteria

- [ ] Selecting 40+ rows of content at 200 columns succeeds without truncation
- [ ] `kSelectionMaxCells` is either raised to 65536 or is runtime-configurable
- [ ] All existing selection tests pass
- [ ] WI 62 warning fires at the new (raised) limit

---

## Notes

**Interdependency with WI 62:** WI 62 adds the warning callback when the limit is hit. This item raises the limit. They can be done independently but landing WI 62 first means users get feedback in the interim. Doing them in the same pass is also fine.
