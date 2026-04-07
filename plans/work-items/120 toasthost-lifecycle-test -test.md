# WI 120 — ToastHost Lifecycle Test

**Type:** Test  
**Severity:** Medium (missing coverage for overlay component)  
**Source:** Gemini review  
**Authored by:** claude-sonnet-4-6

---

## Problem

`ToastHost` has no dedicated lifecycle tests. The existing test infrastructure exercises most hosts but not the toast stacking / expiry / fade path. Claude also noted the early-buffer replay mechanism (toasts buffered before the host exists during init, then replayed) has no test coverage.

From Gemini: "A `ToastHost` lifecycle test for stacking, expiry, fade, and `request_frame()` behavior."

---

## What to Test

```
Stacking:
  - push 3 toasts → all 3 visible in render output
  - push toast while max-stack at limit → oldest evicted or newest dropped (verify policy)

Expiry:
  - push toast with duration 0.1s → after advancing fake clock by 0.2s → toast gone
  - push toast with duration 0s or negative → immediate discard (or clamp, verify policy)

Fade:
  - at T = duration - fade_start: toast alpha begins decreasing
  - at T = duration: toast fully transparent / removed

request_frame():
  - while any toast is fading, request_frame() is called each pump
  - when no toasts remain, request_frame() is NOT called

Early-buffer replay:
  - call push_toast() before ToastHost is attached
  - attach ToastHost
  - verify buffered toasts appear in the host

Config: enable_toast_notifications = false:
  - push_toast() is a no-op; host render output is empty
```

---

## Implementation Notes

- Use a fake clock / injectable `time_now` dependency in `ToastHost::Deps` (add if not present)
- Test file: `tests/toast_host_tests.cpp`
- No Neovim or real renderer required

---

## Acceptance Criteria

- [ ] `tests/toast_host_tests.cpp` exists and covers the above scenarios
- [ ] Fake clock drives timing without real `sleep()`
- [ ] Tests run under `ctest`
- [ ] CI green

---

## Interdependencies

- **WI 125** (overlay registry) should not land until these tests exist.
- Complements **WI 121** (render-tree overlay ordering).
