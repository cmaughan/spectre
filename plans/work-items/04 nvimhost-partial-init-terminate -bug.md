# WI 04 — NvimHost partial init → `std::terminate`

**Type:** bug  
**Severity:** CRITICAL  
**Source:** review-bugs-latest.gpt.md  
**Consensus:** review-consensus.md Phase 1

---

## Problem

`NvimHost::initialize_host()` starts the RPC reader thread (via `rpc_.initialize()`) and the `UiRequestWorker` before `attach_ui()` / `execute_startup_commands()` are called. If either of those later steps returns `false`, `HostManager::create_host_for_leaf()` destroys the partially-initialised host **without calling `shutdown()`**. The joinable `std::thread` members inside `NvimRpc` and `UiRequestWorker` hit the joinable-thread-in-destructor contract, which calls `std::terminate`. The spawned `nvim` child process is also left running as a zombie.

**Trigger:** any startup-time failure after the reader thread starts — e.g. `nvim_ui_attach` rejection, a bad startup command, or a missing `nvim` binary causing `NvimProcess::spawn()` to fail after the RPC object is already initialised.

**Files:**
- `libs/draxul-host/src/nvim_host.cpp` (~line 24 and the initialize_host body)
- `app/host_manager.cpp:444` (create_host_for_leaf)
- `libs/draxul-nvim/src/rpc.cpp:58`
- `libs/draxul-runtime-support/include/draxul/ui_request_worker.h:25`

---

## Implementation Plan

- [ ] In `NvimHost::initialize_host()`, add an RAII rollback guard (similar to the `InitRollback` pattern already used in `App::initialize()`).
- [ ] The rollback guard destructor should call `ui_request_worker_.stop(); rpc_.shutdown(); nvim_process_.shutdown();` if `initialize_host()` exits abnormally.
- [ ] Alternative: make `NvimRpc` and `UiRequestWorker` destructors safe to call even when the thread is joinable (i.e. implicitly join/stop on destruction). Verify this is safe for the overall shutdown order.
- [ ] Add a unit test that constructs `NvimHost`, starts init, injects a failure after the reader thread is up, and verifies no `std::terminate` and no leaked child process.
- [ ] Run under ASan + TSan to confirm no races or use-after-free in the cleanup path.

---

## Interdependencies

- Should be done alongside WI 05, 06, 07 (same RPC pipeline hardening phase).
- WI 16 (host-lifecycle-state-machine test) will cover the double-shutdown and order scenarios once this fix is in.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
