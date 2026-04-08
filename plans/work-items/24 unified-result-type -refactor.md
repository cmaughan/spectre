# WI 24 — Unified `Result<T, Error>` type

**Type:** refactor  
**Source:** review-latest.claude.md  
**Consensus:** review-consensus.md Phase 6

---

## Goal

Replace the three incompatible error-handling patterns currently coexisting in the codebase (`bool + .error()`, `std::optional<T>`, and silent-fail `void`) with a single `Result<T, E>` type that makes error cases impossible to ignore at call sites.

---

## Problem

Claude's review identified at least four distinct error-handling patterns:
- `bool` return with a `.error()` / `.last_error()` accessor
- `std::optional<T>` (missing = failure, no reason)
- Silent fail: function returns `void`, failure logged but not propagated
- `std::expected<T, E>` (C++23) not yet used

This inconsistency means error-path auditing requires knowing which pattern each function uses. Silent fails in particular (clipboard, config reload, atlas overflow) mean users get no feedback. This is flagged in the "Top 10 Bad Things" by Claude as item #3.

---

## Implementation Plan

**Phase A — define the type:**
- [ ] Evaluate whether to use `std::expected<T, E>` (C++23, already available with modern Clang/MSVC) or a custom `Result<T, E>` wrapper. Prefer `std::expected` if the compiler baseline supports it.
- [ ] Define a project-wide error enum or error string type for common failure categories (init failure, RPC error, I/O error, config error).
- [ ] Add a header in `libs/draxul-types/include/draxul/result.h`.

**Phase B — migrate high-value call sites:**
- [ ] `NvimProcess::spawn()` — currently returns `bool`; migrate to `Result<void, SpawnError>`.
- [ ] `NvimRpc::send_request()` — returns `RpcResult`; ensure it's consistent with `Result`.
- [ ] `AppConfig::reload()` — currently silent; migrate to `Result<void, ConfigError>`.
- [ ] `GlyphCache::rasterize_cluster()` — currently returns sentinel; migrate to `Result<AtlasRegion, RasterError>`.

**Phase C — update callers:**
- [ ] For each migrated function, update all call sites to handle the error or explicitly propagate it.
- [ ] Convert previously-silent failures to either return an error to the caller or show a toast notification.

---

## Notes for the agent

- This is a wide-surface refactor; consider subagent parallelism for migrating different libraries.
- Don't migrate everything at once — Phase A + a handful of high-value Phase B sites is a good first PR.
- This change feeds into WI 29 (config-migration-framework) which needs a clean error return from config operations.

---

## Interdependencies

- Should land after Phase 1 bugs (WI 04-07) to avoid merge conflicts in `rpc.cpp`.
- WI 29 (config migration framework) depends on this for clean error returns.
- WI 28 (rpc timeout feedback) will use the error type to surface RPC errors as toasts.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
