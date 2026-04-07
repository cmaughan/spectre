# WI 123 — UiRequestWorker Overlapping Requests and Cancellation Test

**Type:** Test  
**Severity:** Medium (correctness of async request coalescing)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`UiRequestWorker` handles async UI requests (e.g. treesitter scans, resize coalescing). The existing test `17 ui-request-worker-shutdown -test.md` covers shutdown but does not cover:
- What happens when a second request arrives while the first is still in flight
- Whether requests are ordered correctly (FIFO? deduplicated? last-wins?)
- Whether cancellation (via shutdown signal or pane close) is handled cleanly

From Gemini: "A `UiRequestWorker` test for overlapping requests, ordering, and cancellation semantics."

---

## What to Test

```
Ordering:
  - Submit request A then B while A is in flight
  - Verify B is not processed before A completes (or verify documented ordering policy)

Coalescing:
  - Submit request A, then same-type request A' before A starts
  - Verify only one execution occurs (if coalescing is intended)

Cancellation:
  - Submit request A (slow), cancel before it starts → A never executes
  - Submit request A (slow), cancel while in flight → A execution aborts cleanly

Shutdown:
  - Worker shutdown while queue is non-empty → all pending requests cancelled, no crash
  - Worker shutdown while request in flight → waits for completion or aborts safely
```

---

## Implementation Notes

- Use a fake "work" callback with a controllable delay (barrier / semaphore)
- Test file: extend `tests/ui_request_worker_tests.cpp` or create it
- No Neovim or renderer required — pure threading test
- Run under TSan (`mac-tsan` preset, WI 112) to catch races

---

## Acceptance Criteria

- [ ] Ordering, coalescing, cancellation, and shutdown scenarios are covered
- [ ] No deadlock or race detected by TSan
- [ ] Tests run under `ctest`
- [ ] CI green

---

## Interdependencies

- Related to **WI 17** (`ui-request-worker-shutdown` test — extend or complement it).
- **WI 112** (TSan preset) makes it easier to validate threading; add to TSan CI once WI 112 is green.
