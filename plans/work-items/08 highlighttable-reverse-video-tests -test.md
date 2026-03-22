# 08 highlighttable-reverse-video-tests -test

**Priority:** MEDIUM
**Type:** Test (correctness coverage for complex attribute interaction)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`HighlightTable::resolve()` implements reverse-video attribute swapping, but the interaction between reverse-video and partial overrides (e.g., explicit fg with default bg, or explicit bg with default fg) is complex and currently has no targeted test. The normal→visual mode transition changes the effective highlight attributes of many cells simultaneously, making this a high-impact code path for rendering correctness.

---

## Implementation Plan

- [ ] Read `libs/draxul-grid/include/draxul/grid.h` and `libs/draxul-grid/src/grid.cpp` — understand `HlAttr`, `HighlightTable`, and `resolve()`.
- [ ] Read existing highlight-related tests in `tests/` to identify current coverage.
- [ ] Write test cases for `HighlightTable::resolve()` covering:
  - [ ] **Plain reverse-video** — `reverse = true`, default fg and bg → fg and bg are swapped.
  - [ ] **Reverse-video + explicit fg** — `reverse = true`, explicit fg set, default bg → explicit fg becomes bg, default fg becomes fg.
  - [ ] **Reverse-video + explicit bg** — `reverse = true`, default fg, explicit bg set → explicit bg becomes fg, default bg becomes bg.
  - [ ] **Reverse-video + both explicit** — both fg and bg set → both are swapped.
  - [ ] **Reverse-video disabled** — `reverse = false` with same attribute combinations → no swap.
  - [ ] **Normal → Visual mode transition** — simulate a highlight ID change from a non-reverse to a reverse-video entry and verify the resolved colors flip.
- [ ] Add tests to `tests/grid_tests.cpp` or a new `tests/highlight_tests.cpp`.
- [ ] Build and run: `cmake --build build --target draxul-tests && ctest`.

---

## Acceptance

- All listed combinations pass.
- Edge cases (default fg/bg values, sentinel values for "not set") are explicitly exercised.

---

## Interdependencies

- No upstream blockers; self-contained.

---

*claude-sonnet-4-6*
