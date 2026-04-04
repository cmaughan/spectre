---
# WI 65 — HostManager Zoom + Close Interaction Tests

**Type:** test  
**Priority:** high (pane zoom is a recent feature; close interaction is untested)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`HostManager` pane zoom was added recently (commit 6b4408c). The interaction between zoom and close operations is untested. Known scenarios that could produce wrong viewport recomputation:

1. Zoom a pane, then close the **zoomed** pane — should un-zoom and restore other pane(s)
2. Zoom a pane, then close a **different** pane — zoomed pane should remain zoomed with updated viewport
3. Zoom a pane, then re-zoom (toggle) — should restore original layout

---

## Investigation Steps

- [ ] Read `app/host_manager.h` and `app/host_manager.cpp` zoom implementation
- [ ] Read the `SplitTree` viewport recomputation path used after close
- [ ] Identify the `HostManager::Deps` fake/test seam and existing test fixtures (check `tests/host_manager_test.cpp` if it exists, or nearest equivalent)

---

## Test Cases

- [ ] `zoom_pane(A)`, then `close_pane(A)` → other pane gets full viewport; no zoom state lingers
- [ ] `zoom_pane(A)`, then `close_pane(B)` → pane A remains zoomed at full viewport; split tree is consistent
- [ ] `zoom_pane(A)`, then `zoom_pane(A)` again → toggle restores original split layout
- [ ] `zoom_pane(A)` with only one pane open → behaviour is defined (noop or already full)
- [ ] Focus navigation after zoom close uses correct pane reference

---

## Implementation

- [ ] Locate or create `tests/host_manager_zoom_test.cpp`
- [ ] Use `FakeWindow` and `FakeGridPipelineRenderer` (or equivalent fakes) to drive `HostManager`
- [ ] Add `TEST_CASE` blocks for each scenario above

---

## Acceptance Criteria

- [ ] All scenarios pass with correct viewport dimensions verified via `SplitTree::viewport_for()`
- [ ] No dangling host pointer after zoomed-pane close
- [ ] No zoom state flag is set after all panes except one are closed

---

## Interdependency

WI 45 (pane-management-actions) touches `HostManager` — co-ordinate or sequence to avoid merge conflicts.
