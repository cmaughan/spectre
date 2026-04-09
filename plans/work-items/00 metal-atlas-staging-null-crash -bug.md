# WI 00 — metal-atlas-staging-null-crash

**Type:** bug  
**Priority:** CRITICAL (null-pointer write → crash under memory pressure)  
**Platform:** macOS only  
**Source:** review-bugs-consensus.md — BUG-01 (GPT)

---

## Problem

`MetalRenderer::flush_pending_atlas_uploads()` in `libs/draxul-renderer/src/metal/metal_renderer.mm` (lines 1000–1014) grows the per-frame atlas staging buffer by calling `[device newBufferWithLength:...]`. If Metal returns `nil` (low memory, device lost, resource limit), line 1004 immediately commits the new size into `atlas_staging_sizes_[slot]`. Then line 1008 calls `[atlas_staging_[slot].get() contents]` on the nil buffer, returning `nullptr`. Line 1014 does `std::memcpy(dst + offset, ...)` on that null pointer — guaranteed crash on the next atlas upload under memory pressure.

---

## Investigation

- [ ] Read `libs/draxul-renderer/src/metal/metal_renderer.mm` around lines 998–1016 to confirm the allocation, size-commit, and memcpy sequence.
- [ ] Check if `atlas_staging_[slot]` is an ObjC smart pointer; confirm `.get()` on nil returns nil.
- [ ] Confirm `[nil contents]` returns `nullptr` in ObjC (it does — all messages to nil return zero/nil).
- [ ] Search the file for any other `newBufferWithLength:` calls that lack nil checks.

---

## Fix Strategy

- [ ] Move the size update to after a successful allocation check:
  ```cpp
  id<MTLBuffer> buf = [device_.get() newBufferWithLength:total_bytes
                                                 options:MTLResourceStorageModeShared];
  if (!buf) {
      DRAXUL_LOG_ERROR(LogCategory::Renderer,
          "flush_pending_atlas_uploads: staging buffer alloc failed (%zu bytes)", total_bytes);
      return;
  }
  atlas_staging_[slot].reset(buf);
  atlas_staging_sizes_[slot] = total_bytes;
  ```
- [ ] While in the file, apply the same guard to `MetalGridHandle::upload_state()` (BUG-03) and `initialize()` (BUG-02) — fix all three Metal allocation paths in one PR.

---

## Acceptance Criteria

- [ ] `flush_pending_atlas_uploads()` does not crash when Metal returns nil; logs an error and returns cleanly.
- [ ] `atlas_staging_sizes_[slot]` is only updated after a confirmed non-nil allocation.
- [ ] Build passes: `cmake --build build --target draxul draxul-tests`.
- [ ] Smoke test passes: `py do.py smoke`.
- [ ] No new ASan/UBSan findings from the changes.
