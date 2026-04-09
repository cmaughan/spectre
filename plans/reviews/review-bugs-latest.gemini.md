I will start by listing the files in the target directories to understand the scope of the investigation.
I will now perform a series of targeted `grep_search` calls to identify common bug patterns across the codebase, focusing on potential memory safety issues, concurrency hazards, and logic errors.
I will now examine `app/app.cpp` and `libs/draxul-nvim/src/ui_events.cpp` for potential `nullptr` dereferences and out-of-bounds access. I'll also investigate the `memcpy` operations in `sign_label_atlas.cpp` and `megacity_render_vk.cpp` to ensure proper validation and size checks.
I will now examine `libs/draxul-grid/src/grid.cpp` to verify the handling of double-width characters and check for potential out-of-bounds access. Additionally, I'll investigate the thread safety of `app/app.cpp` by searching for `std::thread` and `std::mutex` usage, as the project employs both a reader thread and a main thread.
I'll examine `handle_grid_line` in `libs/draxul-nvim/src/ui_events.cpp` by reading further into the file. I'll also check `app/app.cpp` to see how it manages the Neovim process lifecycle, specifically searching for `on_nvim_exit` to ensure the app handles unexpected Neovim exits gracefully.
I will examine `libs/draxul-renderer/src` to check for potential GPU resource leaks, particularly in files like `vk_renderer.cpp`. I also want to investigate `cluster_cell_width` in `libs/draxul-grid/src/unicode.cpp` (or its equivalent) to see how it handles character widths, especially the edge case where a double-width character might be truncated at the end of a line.
I'll check `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` and `libs/draxul-grid/src/unicode.cpp` as planned.
I can't find `unicode.cpp` in `draxul-grid/src`. I'll search for it or the `cluster_cell_width` function, possibly in `draxul-types` since I saw `draxul/unicode.h` mentioned in `ui_events.cpp`.
I'll examine the inline function in `libs/draxul-types/include/draxul/unicode.h`.
`cluster_cell_width` correctly handles Neovim's empty continuation cells by returning 1, which increments `col` by 1 in `handle_grid_line`. I'll now investigate `app/split_tree.cpp` for potential layout logic errors, and also check `libs/draxul-config/src/config.cpp` (or its equivalent) and `scripts/gen_deps.py` for any issues.
In `SplitTree::close_leaf`, the destruction of `parent` when replacing it with `sibling` seems safe as long as `parent` and its members aren't used afterward. However, if `parent` isn't the root and `grandparent` is null, the tree could be corrupted. I'll now examine `SplitTree::recompute_node` in `app/split_tree.cpp` to see how it handles `kDividerWidth`.
Searching for `recompute_node`. It must be further down.
`SplitTree::recompute_node` correctly handles cases where the divider consumes all available space by assigning zero width to children. In `SplitTree::swap_leaves`, `focused_id_` is updated to follow the swapped content, which seems intentional. `SplitTree::find_neighbor` also appears to correctly handle spatial navigation. I'll now check `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` for any `vkDestroy` calls to ensure all Vulkan resources are properly cleaned up.
Checking `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` around lines 461 and 463 to see if the same semaphore is being destroyed twice.
The `vkDestroySemaphore` calls are in different loops, so they're fine. I'll now check `libs/draxul-host/src/terminal_host_base.cpp` for terminal emulation issues and `libs/draxul-nvim/src/nvim_process.cpp` to see how it spawns Neovim, specifically looking for `fork`/`exec` on macOS and `CreateProcess` on Windows.
In `NvimProcess::spawn` on Windows, using `cmd.data()` with `CreateProcessA` is safe in C++20 as it's null-terminated and `CreateProcessA` can modify it. `NvimProcess::shutdown` aggressively terminates the process after 2 seconds, but double-shutdown is avoided by checking `impl_->started_`. `proc_info_` is safely zero-initialized. I'll now examine the macOS `NvimProcess::spawn` implementation.
`NvimProcess::spawn` on POSIX safely handles `argv` pointers because `argv_storage` is pre-allocated. However, `NvimProcess::write` and `NvimProcess::read` on POSIX fail to handle `EINTR`, potentially treating signal interruptions as fatal errors. I'll now examine `libs/draxul-nvim/src/rpc.cpp` to see how `NvimRpc` handles these return values.
In `NvimRpc::request`, if `write` returns `false` because of an unhandled `EINTR`, `read_failed_` is incorrectly set to `true`, killing the RPC transport. I need to check `NvimRpc::reader_thread_func` in `rpc.cpp` next.
In `NvimRpc::reader_thread_func`, a `read` failure from `EINTR` incorrectly kills the transport. I also need to check if there's a limit on the `accum` buffer size to prevent it from growing indefinitely with large msgpack messages. Reading more of `reader_thread_func` now.
In `NvimRpc::reader_thread_func`, the `accum` buffer correctly appends `n` bytes from `read_buf_`. `read_buf_` is properly initialized and resized. I'll now check the definition of `MpackValue` in `libs/draxul-types/include/draxul/mpack_codec.h` to see how it handles storage, specifically for functions like `make_int`.
Reading `libs/draxul-nvim/include/draxul/mpack_codec.h` now.
`MpackValue` isn't defined here, and the file includes `<draxul/nvim_rpc.h>`. I'll check `libs/draxul-nvim/include/draxul/nvim_rpc.h` for the definition.
`MpackValue::as_int()` might cause logic errors by casting large `uint64_t` values to negative `int64_t`, though this is often expected in msgpack-RPC. `MpackValue::Type()` and its accessors are safe as they handle exceptions or use `static_assert` for bounds checking. I'll now check `libs/draxul-host/src/terminal_host_base.cpp` for signal-unsafe calls or data races, and also verify `app/app.cpp` for potential `nullptr` dereferences.
`TerminalHostBase::pump()`'s 8ms budget is reasonable for 60Hz. `TerminalHostBase::dispatch_action` has a minor flaw: a second `paste` overwrites an existing `pending_paste_`. Regarding `std::bind_front` in `TerminalHostBase`, `vt_parser_` as a member should be destroyed before the base class, but I'll check its declaration order in `terminal_host_base.h` to ensure no use-after-free risks.
In `TerminalHostBase`, members are destroyed in reverse order of their declaration, meaning `vt_parser_` is destroyed after `alt_screen_` but before `current_cwd_`. Since `vt_parser_` uses `this` in callbacks triggered by `pump()`, I need to ensure `GridHostBase` doesn't have a thread calling `pump()` while `TerminalHostBase` is being destroyed. I'll check `libs/draxul-host/include/draxul/grid_host_base.h` for any such threading.
`GridHostBase` is main-thread driven via `pump()`, so use-after-free during destruction is unlikely as `App` manages host lifetimes. I've spotted a potential bug in `Grid::scroll` in `libs/draxul-grid/src/grid.cpp`, so I'll examine `scroll_rows` and `scroll_cols` next.
`CellText::assign` safely truncates to `valid_len`, but I need to verify `CellText`'s definition and `kMaxLen`. `Grid::resize` for a 10000x10000 grid would push 100 million entries into `dirty_cells_`, potentially causing a massive 800MB memory spike and performance stall. I'll now check the definitions for `CellText` and `DirtyCell`.
Reading `libs/draxul-grid/include/draxul/grid.h`.
`CellText`'s `valid_len` check is safe. `Grid::clear()` has a scalability issue that could cause an 800MB OOM on a 10000x10000 grid. `Grid::scroll`'s boundary checks for `index + 1` and `index - 1` are safe. I'll now examine `libs/draxul-renderer/src/renderer_state.cpp` to verify the bounds for the `gpu_cells_` `memcpy` at line 33.
`RendererState::copy_dirty_cells_to` and `RendererState::copy_overlay_region_to` appear safe. I'll now examine the rest of `RendererState::set_grid_size` to check for bugs during grid resizing.
`RendererState::set_grid_size` correctly preserves `old_cells` data while updating `pos` and `size`. However, if `cell_w_` or `cell_h_` are 0 when `set_grid_size` is called (perhaps before `set_cell_size` initializes them), all cells will have the same position. I'll check `renderer_state.h` for their initial values.
Reading `libs/draxul-renderer/include/draxul/renderer_state.h`.
`cell_w_` and `cell_h_` are safely initialized. `MpackValue::Type()`'s `static_assert` is also solid. I'll now examine `app/input_dispatcher.cpp` and `libs/draxul-host/src/local_terminal_host.cpp` for input routing and terminal host issues.
Checking `local_terminal_host.cpp`, specifically for `selection_`, to see how scrollback and selection are handled in this 18KB file.
`LocalTerminalHost::on_mouse_event` safely handles `copy_on_select` because the main thread manages all grid mutations and input. I'll now check `libs/draxul-host/src/scrollback_buffer.cpp` for potential OOM or out-of-bounds access.
`ScrollbackBuffer::resize` and `update_display` appear safe, with `idx` correctly guarded. `save_live_snapshot` is limited by grid dimensions. I'll now examine `libs/draxul-host/src/conpty_process.cpp` for Windows process issues and `libs/draxul-host/src/unix_pty_process.cpp` for POSIX/macOS.
`UnixPtyProcess::spawn` on POSIX is safe regarding `argv` pointers. However, `UnixPtyProcess::shutdown` can block the main thread for up to 600ms, unlike `NvimProcess::shutdown` which uses a detached thread. I'll now examine `UnixPtyProcess::write` for any critical bugs.
`UnixPtyProcess::write` and `reader_main` correctly handle `EINTR`, unlike `NvimProcess`. `NvimRpc::request`'s timeout handling also appears sound. I'll now examine `libs/draxul-host/src/terminal_host_base_csi.cpp`, specifically `csi_margins`, as complex CSI sequences can sometimes cause out-of-bounds issues if parameters aren't properly validated.
`TerminalHostBase::csi_margins` correctly handles cases where `grid_rows()` is 1, preventing impossible scroll regions. `csi_dsr` uses `snprintf` safely with `std::string::data()`. I'll now examine `scripts/gen_deps.py` for potential shell injection or path traversal vulnerabilities.
`gen_deps.py` uses `subprocess.run` safely, but I'll check if the `--output` parameter is vulnerable to path traversal.
`args.output` in the script could resolve to any directory, but since it's a dev tool, it's not a major security risk. `perf_timing.cpp` uses mutexes for thread safety. `Grid::scroll` has safe bounds checks. I'll now check `libs/draxul-renderer/include/draxul/renderer.h` to see if `renderer_.grid()` could be null in `App::on_redraw_flush`, which would cause a crash.
`App::on_redraw_flush` is safe since `renderer_` is guaranteed non-null if initialized. `UiEventHandler::handle_grid_scroll` correctly parses Neovim's `grid_scroll` event. `TerminalHostBase::csi_sgr` properly resets attributes when parameters are empty. I'll now examine `libs/draxul-types/src/perf_timing.cpp` to ensure the static `collector` is thread-safe for multi-threaded access.
`RuntimePerfCollectorImpl` seems thread-safe with its mutex-locked public methods. I'll now verify `record_sample` in `libs/draxul-types/src/perf_timing.cpp` to be sure.
`report_timing` is safe because it's mutex-locked. It uses `&tag` as a map key, which works if `PerfTimingTag` has a stable address, typically guaranteed by `static` in a `PERF_MEASURE` macro. I'll now verify this in `libs/draxul-types/include/draxul/perf_timing.h`.
`PERF_MEASURE()` in a header will create distinct static tags per translation unit, causing `RuntimePerfCollectorImpl` to treat them as different functions despite having the same name. I'll check `resolve_tag` in `libs/draxul-types/src/perf_timing.cpp` to see how it handles this.
`ScopedPerfMeasure` is thread-safe for `enabled()` changes. `now_microseconds()` is also safe for session-duration calculations. The use of `const PerfTimingTag*` in `frame_metrics_` correctly handles template instantiations. I'll now double-check `libs/draxul-renderer/include/draxul/renderer.h` to see if `IGridRenderer` has a `flush()` method, as called in `App::on_redraw_flush`.
`IGridRenderer` inherits from `IBaseRenderer`. I'll check `libs/draxul-renderer/include/draxul/base_renderer.h` now.
`IBaseRenderer` and `IGridRenderer` lack a `flush()` method. I'll check `app/app.cpp` to see what's actually being called, as my previous thought might have been slightly off.
`grep_search` didn't find `flush` in `app/app.cpp`, which is odd. I'll re-examine `app/app.cpp` to see what `on_redraw_flush` actually calls.
`on_redraw_flush` is missing from `app/app.cpp`. I'll search for `on_flush` instead.
`UiEventHandler` calls `on_flush`, which is a callback set in `NvimHost`. I'll check `libs/draxul-host/src/nvim_host.cpp` to see how it's defined and used.
`NvimHost::on_flush()` triggers `GridHostBase::flush_grid()`, which in turn calls `grid_pipeline_->flush()` with any dirty cells. I'll now examine `GridRenderingPipeline::flush` in `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp`.
In `GridRenderingPipeline::upload_atlas`, I need to verify if `glyph_atlas_.atlas_dirty_rect()` stays within bounds, as exceeding `atlas_height` or `atlas_width` would cause an out-of-bounds read. `atlas_upload_scratch_` is safely resized, but `src` access is risky. I'll examine `libs/draxul-font/src/glyph_cache.cpp` to confirm.
Checking `glyph_cache.cpp`.
`bitmap_to_rgba`'s mono and gray modes seem safe, as FreeType's pitch should cover the accesses. I'm now going to investigate the atlas packing logic in `libs/draxul-font/src/glyph_cache.cpp` for any critical bugs.
`reserve_region` and its related loops seem safe even with zero dimensions. The composite blending uses slow floating-point math, but it's only on cache misses, so it's a performance trade-off rather than a bug. `expand_dirty_rect` is correctly implemented. I'm moving on to `libs/draxul-font/src/font_resolver.cpp` to investigate potential infinite loops in font fallback.
Here is the bug hunting report for the Draxul repository:

# Draxul Bug Hunting Report

This report focuses on correctness issues, defects, and potential runtime failures identified during a direct scan of the source files.

## Summary of Findings

| Severity | Category | Description |
| :--- | :--- | :--- |
| **CRITICAL** | Memory / Performance | `Grid::resize` and `Grid::clear` can cause 800MB+ memory spikes and massive stalls on large grids. |
| **HIGH** | Concurrency / Error Handling | `NvimProcess` read/write (POSIX) fails to handle `EINTR`, potentially killing the RPC transport on signals. |
| **HIGH** | Concurrency / Error Handling | `NvimRpc` incorrectly treats signal-interrupted writes as fatal transport failures. |
| **MEDIUM** | Logic Error | `TerminalHostBase` silent data loss during pending paste. |
| **MEDIUM** | Data Loss | `Grid` truncates Unicode clusters longer than 32 bytes. |
| **MEDIUM** | Resource Management | `NvimRpc` reader thread lacks a hard limit on the accumulation buffer. |
| **MEDIUM** | Platform-Specific | `UnixPtyProcess::shutdown` can block the main thread for up to 600ms. |
| **MEDIUM** | Numeric Precision | `MpackValue::as_int()` performs unsafe cast of large unsigned integers to signed. |

---

## CRITICAL Findings

### 1. Massive Memory Spike and Stall on Large Grid Operations
- **File**: `libs/draxul-grid/src/grid.cpp` (Lines 378, 447)
- **Severity**: **CRITICAL**
- **What goes wrong**: The `Grid::clear` and `Grid::resize` methods iterate over every cell in the grid and call `mark_dirty_index(int index)`. This function unconditionally pushes the index into a `std::vector<DirtyCell> dirty_cells_`. While `kMaxGridDim` is capped at 10,000, a 10,000x10,000 grid contains 100 million cells. Calling `clear()` on such a grid will result in a 100-million-entry vector allocation (approx. 800MB of `DirtyCell` structs) and a massive CPU stall during the `push_back` loop and subsequent reallocations.
- **Suggested fix**: Use a more compact dirty tracking mechanism (e.g., bitset or dirty ranges) for mass-clear operations, or skip individual dirty marking and set a `full_grid_dirty` flag.

---

## HIGH Findings

### 2. POSIX `NvimProcess` Read/Write Fails to Handle `EINTR`
- **File**: `libs/draxul-nvim/src/nvim_process.cpp` (Lines 434, 445)
- **Severity**: **HIGH**
- **What goes wrong**: In the POSIX implementation of `NvimProcess::write` and `NvimProcess::read`, any signal received by the process during a blocking I/O operation will cause the system call to return -1 with `errno == EINTR`. The current code treats any `n <= 0` or `n < 0` as a fatal error and returns `false` or `-1`. This will cause the `NvimRpc` layer to declare the transport broken and shut down the connection to Neovim unnecessarily.
- **Suggested fix**: Add `if (errno == EINTR) continue;` to the loop in `write` and handle it in `read`.

### 3. `NvimRpc` Incorrectly Handles Interrupted Writes
- **File**: `libs/draxul-nvim/src/rpc.cpp` (Lines 186, 245)
- **Severity**: **HIGH**
- **What goes wrong**: When `impl_->process_->write(...)` returns `false` (which happens on `EINTR` or `EAGAIN` as noted above), `NvimRpc` immediately sets `impl_->read_failed_ = true` and notifies all waiters. This permanently kills the RPC transport for the session due to a transient, recoverable interruption.
- **Suggested fix**: Ensure the underlying `write` handles transient errors, or refine the `NvimRpc` logic to only treat verified broken pipes/connection closes as fatal.

---

## MEDIUM Findings

### 4. Silent Data Loss During Pending Paste
- **File**: `app/terminal_host_base.cpp` (Lines 149, 153)
- **Severity**: **MEDIUM**
- **What goes wrong**: In `TerminalHostBase::dispatch_action`, if a large paste (exceeding `paste_confirm_lines`) is triggered, it is stored in `pending_paste_` awaiting user confirmation. If the user triggers another paste (of any size) before confirming the first one, the original `pending_paste_` is overwritten and lost without warning.
- **Suggested fix**: Check `if (!pending_paste_.empty())` and either append, warn the user, or automatically confirm the previous paste.

### 5. Unicode Cluster Truncation
- **File**: `libs/draxul-grid/src/grid.cpp` (Lines 234)
- **Severity**: **MEDIUM**
- **What goes wrong**: `CellText::assign` truncates any UTF-8 cluster longer than 32 bytes. While rare, complex ZWJ sequences (e.g., family emojis with skin tone modifiers) or long combining character chains can exceed this limit, leading to visual corruption or "phantom" characters if the trailing bytes were part of a valid cluster.
- **Suggested fix**: As noted in the `TODO`, consider a fallback `std::string` or heap allocation for exceptionally large clusters, or increase `kMaxLen` if memory allows.

### 6. Unbounded Accumulation in RPC Reader Thread
- **File**: `libs/draxul-nvim/src/rpc.cpp` (Lines 443)
- **Severity**: **MEDIUM**
- **What goes wrong**: The `NvimRpc::reader_thread_func` appends all incoming bytes to a `std::vector<uint8_t> accum`. While it compacts the buffer periodically, it does not have a hard upper limit on the total size of `accum`. A malformed or malicious msgpack stream that never provides a valid complete message could cause the buffer to grow until the process runs out of memory.
- **Suggested fix**: Implement a maximum buffer size for `accum` and abort the transport if it is exceeded.

### 7. Main-Thread Block During `UnixPtyProcess` Shutdown
- **File**: `libs/draxul-host/src/unix_pty_process.cpp` (Lines 111, 131)
- **Severity**: **MEDIUM**
- **What goes wrong**: `UnixPtyProcess::shutdown` is called on the main thread (e.g., when closing a tab). It performs a synchronous timed wait for the child process to exit, with a total potential block of up to 600ms (100ms for SIGTERM, 500ms for SIGKILL). If a shell or subprocess is stuck, the entire GUI will freeze for this duration.
- **Suggested fix**: Offload the timed wait and SIGKILL escalation to a detached background thread, similar to how `NvimProcess` handles it.

### 8. Unsafe Cast in `MpackValue::as_int()`
- **File**: `libs/draxul-nvim/include/draxul/nvim_rpc.h` (Lines 83)
- **Severity**: **MEDIUM**
- **What goes wrong**: `MpackValue::as_int()` casts a `uint64_t` storage to `int64_t` without checking if the value exceeds `INT64_MAX`. For msgpack messages containing large unsigned integers (e.g., specific bitmasks or large IDs), this returns a negative value, potentially causing logic errors in callers that expect only positive integers.
- **Suggested fix**: Add a range check or return `std::optional<int64_t>`/`uint64_t` to handle large unsigned values explicitly.
