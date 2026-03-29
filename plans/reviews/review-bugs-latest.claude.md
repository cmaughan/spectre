## Draxul Bug Review — 2026-03-29

The agent read 83 files across `app/`, `libs/`, `tests/`, and `shaders/`. Here are the findings ranked by severity:

---

### CRITICAL

**BUG-01 — `libs/draxul-megacity/src/megacity_host.cpp` ~line 337**
**Data race on `renderer_config_`**

`route_worker_loop()` runs on the background `route_thread_` and reads `renderer_config_` without holding `route_mutex_`. The main thread writes it freely. Torn reads → stale routing decisions or memory corruption.

**Fix:**
```cpp
MegaCityRendererConfig config_snapshot;
{
    std::lock_guard<std::mutex> lk(route_mutex_);
    config_snapshot = renderer_config_;
}
// pass config_snapshot to build_city_routes_for_selection
```

---

**BUG-02 — `libs/draxul-types/src/bmp.cpp` line 46**
**Signed integer overflow (UB) in image size computation**

```cpp
const auto image_size = static_cast<uint32_t>(frame.width * frame.height * 4);
```
Both `frame.width` and `frame.height` are `int`. Multiplication is done in `int`, which overflows for large frames (HiDPI, render tests at 2×).

**Fix:**
```cpp
const auto image_size = static_cast<uint32_t>(
    static_cast<uint64_t>(frame.width) * frame.height * 4);
```

---

**BUG-03 — `libs/draxul-types/src/perf_timing.cpp` (ScopedPerfMeasure destructor)**
**Unsigned underflow corrupts timing averages permanently**

`end_microseconds - start_microseconds_` wraps to ~`UINT64_MAX` on clock discontinuities (NTP adjustment, TSC migration). This passes the `microseconds == 0` guard and inserts a ~584,542-year measurement into the rolling average.

**Fix:**
```cpp
if (end_microseconds > start_microseconds_) {
    runtime_perf_collector().report_timing(*tag_, end_microseconds - start_microseconds_);
}
```

---

### HIGH

**BUG-04 — `libs/draxul-megacity/src/megacity_render_vk.cpp` ~line 460**
**GPU resource leak on partial failure in `create_sampled_image`**

If `vkCreateImageView` succeeds but `vkCreateSampler` fails, the function returns `false` without destroying the image or view. Each GPU memory pressure event leaks a VMA image + VkImageView.

**Fix:** Add explicit cleanup on each failure path (image → view → sampler in reverse).

---

**BUG-05 — `libs/draxul-megacity/src/megacity_render_vk.cpp` line 485**
**`ImageResource::size` stores `width` (pixels) instead of byte size**

```cpp
image.size = width;  // should be width * height * 4 bytes
```
Any code reading `.size` for transfer sizing or bounds checking uses a value ~4× too small.

**Fix:**
```cpp
image.size = static_cast<VkDeviceSize>(width) * height * 4;
```

---

**BUG-06 — `libs/draxul-megacity/src/megacity_render_vk.cpp` ~line 299**
**`upload_mesh` missing empty-input guard — Vulkan spec violation**

`stream_transient_mesh` guards against empty vertex/index data; `upload_mesh` does not. `vkCreateBuffer` with `size == 0` is undefined per spec and triggers validation errors.

**Fix:**
```cpp
if (vertices.empty() || indices.empty()) { out_mesh = {}; return true; }
```

---

**BUG-07 — `libs/draxul-host/src/terminal_host_base.cpp` ~line 233**
**Scrollback buffer cells excluded from `compact_attr_ids` → wrong colors in scrollback**

`compact_attr_ids()` omits scrollback rows when building the remap table. Any `hl_attr_id` exclusive to scrollback is absent, so `HighlightRemapper` falls back to 0 (default highlight) — all scrollback content renders with no syntax colors.

**Fix:** Iterate `scrollback_buffer_` rows and add their `hl_attr_id` values to the active-attrs set before computing the remap table.

---

**BUG-08 — `libs/draxul-host/src/scrollback_buffer.cpp` ~line 110**
**`restore_live_snapshot` uses stale column stride after terminal resize**

After a resize, the current `cells_` array has a different stride than `live_snapshot_cols_`. Restoring with the old stride writes cells at wrong offsets → corrupted terminal display.

**Fix:** Save snapshot dimensions with the snapshot data; use `min(snapshot_cols, current_cols)` as the column count during restore.

---

**BUG-09 — `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` (`create_sync_objects`)**
**Partial sync-object creation leaks already-created semaphores/fences on failure**

If any `vkCreateSemaphore`/`vkCreateFence` fails mid-loop, all objects created in prior iterations are leaked (never destroyed, vectors cleared on reinit).

**Fix:** On failure, destroy all objects created so far before returning `false`.

---

### MEDIUM

**BUG-10 — `libs/draxul-megacity/src/megacity_host.cpp` ~lines 351, 895**
**Raw `callbacks_` pointer accessed from background threads without lock**

Background threads call `callbacks_->request_frame()` with no lock. Any future exception before `join()` or shutdown reordering → use-after-free.

**Fix:** Store `callbacks_` as `std::weak_ptr` and lock before use.

---

**BUG-11 — `libs/draxul-nvim/src/nvim_process.cpp` ~line 361 (POSIX)**
**`waitpid` after SIGKILL blocks indefinitely if nvim is in D-state**

SIGKILL won't be delivered to a process stuck in uninterruptible kernel wait (disk I/O, NFS). `waitpid(0)` hangs the UI on exit.

**Fix:** Replace with a timed poll loop using `WNOHANG`, giving up after ~5 seconds.

---

**BUG-12 — `libs/draxul-font/src/glyph_cache.cpp` (`rasterize_cluster`)**
**Signed integer overflow when all glyph bitmaps are empty**

```cpp
int bbox_left = INT_MAX, bbox_right = INT_MIN;
// if no glyph produces ink, these are never updated:
cluster_width = bbox_right - bbox_left; // INT_MIN - INT_MAX → UB
```

**Fix:** After the bbox loop, if `bbox_right < bbox_left`, return early with an empty region.

---

**BUG-13 — `app/app.cpp` ~line 687**
**Float equality for PPI change detection**

```cpp
if (new_ppi == display_ppi_) return;
```
Floating-point representation differences can cause a missed relayout at the wrong scale.

**Fix:**
```cpp
if (std::abs(new_ppi - display_ppi_) < 0.5f) return;
```

---

| # | File | Severity | Category |
|---|------|----------|----------|
| 01 | `megacity_host.cpp` | CRITICAL | Data race |
| 02 | `bmp.cpp` | CRITICAL | Integer overflow/UB |
| 03 | `perf_timing.cpp` | CRITICAL | Unsigned underflow |
| 04 | `megacity_render_vk.cpp` | HIGH | GPU resource leak |
| 05 | `megacity_render_vk.cpp` | HIGH | Wrong value stored |
| 06 | `megacity_render_vk.cpp` | HIGH | Vulkan spec violation |
| 07 | `terminal_host_base.cpp` | HIGH | Logic error (wrong colors) |
| 08 | `scrollback_buffer.cpp` | HIGH | Wrong stride post-resize |
| 09 | `vk_renderer.cpp` | HIGH | Sync object leak |
| 10 | `megacity_host.cpp` | MEDIUM | Fragile shutdown ordering |
| 11 | `nvim_process.cpp` | MEDIUM | Unbounded blocking wait |
| 12 | `glyph_cache.cpp` | MEDIUM | Signed overflow/UB |
| 13 | `app.cpp` | MEDIUM | Float equality |
