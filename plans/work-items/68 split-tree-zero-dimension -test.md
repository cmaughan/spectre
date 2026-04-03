---
# WI 68 — SplitTree Zero-Dimension Window Tests

**Type:** test  
**Priority:** medium (edge case; could cause downstream assert or UB)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`SplitTree::recompute()` uses `std::max(0, w - div_w)` when computing sub-viewport widths. A zero-dimension window (width=0, height=0) may still produce unexpected viewport calculations for downstream consumers. This can happen on minimise or during window creation before the first resize event.

---

## Investigation Steps

- [ ] Read `app/split_tree.h` and `app/split_tree.cpp`
- [ ] Find `reset()`, `recompute()`, and `viewport_for()` implementations
- [ ] Trace what happens when `recompute(0, 0)` is called on a two-pane tree
- [ ] Check whether any downstream consumer (renderer, host) asserts or divides by width/height

---

## Test Cases

- [ ] `reset(0, 0)` on a single-pane tree → `viewport_for()` returns `{0, 0, 0, 0}`, no UB
- [ ] `recompute(0, 0)` on a two-pane tree → both viewports are `{0, 0, 0, 0}`, divider at 0
- [ ] `recompute(0, 0)` followed by `recompute(800, 600)` → layout recovers correctly
- [ ] `set_divider_ratio(0.5f)` then `recompute(0, 0)` → divider pixel position is 0, not negative
- [ ] Leaf viewport is never negative in width or height for any input

---

## Implementation

- [ ] Add `TEST_CASE("SplitTree zero dimensions")` to the existing `tests/split_tree_test.cpp`

---

## Acceptance Criteria

- [ ] All scenarios return valid (non-negative, non-garbage) viewport structs
- [ ] No assertions fire in debug builds
- [ ] The recover-from-zero test passes, confirming the tree is not permanently corrupted

---

## Notes

This is a low-risk test addition — no production code changes expected unless a bug is found.
