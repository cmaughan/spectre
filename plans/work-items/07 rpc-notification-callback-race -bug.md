# WI 07 — `on_notification_available` / `on_request` assigned after reader thread starts

**Type:** bug  
**Severity:** HIGH  
**Source:** review-bugs-latest.gemini.md (CRITICAL), review-bugs-latest.gpt.md (HIGH)  
**Consensus:** review-consensus.md Phase 1

---

## Problem

In `NvimHost::initialize_host()`, the RPC reader thread is started via `rpc_.initialize()` **before** `on_notification_available` and `on_request` `std::function` members are assigned. These members are `public` in `NvimRpc` (`nvim_rpc.h:95`).

If Neovim sends a notification or request immediately upon startup (e.g. during UI attach negotiation), the reader thread reads a partially-initialised `std::function`, causing undefined behaviour (likely a null-function call crash or data race).

`std::function` is not thread-safe for concurrent assignment vs. invocation. Even if the window is small in practice, this is a genuine data race that TSan will flag.

**Files:**
- `libs/draxul-nvim/src/rpc.cpp:58` (reader thread start)
- `libs/draxul-host/src/nvim_host.cpp` (callback assignment location)
- `libs/draxul-nvim/include/draxul/nvim_rpc.h` (~line 95)

---

## Implementation Plan

- [ ] Option A (preferred): Pass `on_notification_available` and `on_request` as constructor/initialisation arguments to `NvimRpc` so they are set before the reader thread starts. Update `NvimHost::initialize_host()` accordingly.
- [ ] Option B: In `NvimRpc::initialize()`, accept callback arguments directly rather than relying on the caller to assign public members afterward.
- [ ] Either way: make `on_notification_available` and `on_request` private/protected and only settable via an initialisation API, preventing the time-of-check-to-time-of-use window.
- [ ] Add a TSan test that constructs `NvimRpc`, starts a reader, immediately fires a notification, and asserts no race is detected.
- [ ] Verify the fix with `cmake --preset mac-tsan && ctest -R draxul-tests`.

---

## Interdependencies

- Batch with WI 04, 05, 06 (Phase 1 RPC hardening) — all in `rpc.cpp` / `nvim_host.cpp`.
- WI 112 (tsan-build-preset, active) provides the TSan infrastructure needed to validate this fix.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
