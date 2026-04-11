After a deep-dive audit of the Draxul codebase, I have identified several bugs and correctness issues. The findings are ranked by severity, with the most critical issues first.

### CRITICAL: Memory Safety & Correctness

1. **`UnixPtyProcess` Use-After-Free during shutdown**
   - **File and Line Number**: `libs/draxul-host/src/unix_pty_process.cpp`, Line 158 (inside `shutdown()` and `reader_main()`)
   - **Severity**: CRITICAL (UB/Crash)
   - **What goes wrong**: In `shutdown()`, the reader thread is moved to a local variable `reader_copy`, which is then captured by a detached thread that waits for the process to exit and then joins the reader. However, `shutdown()` returns immediately after detaching this thread. If the `UnixPtyProcess` object is destroyed (e.g., in the destructor), the reader thread (still running `reader_main`) will access `this->reader_running_` and `this->on_output_available_` on a destroyed object.
   - **Suggested fix**: Synchronously join the reader thread in `shutdown()` after signaling it to exit, or ensure the detached thread manages the entire object's lifetime via `shared_from_this()`.

### HIGH: Buffer Overruns & Logic Errors

2. **`TerminalHostBase::csi_dsr` OOB read / Buffer Overrun**
   - **File and Line Number**: `libs/draxul-host/src/terminal_host_base_csi.cpp`, Line 338
   - **Severity**: HIGH (OOB Read/Memory Corruption)
   - **What goes wrong**: The code uses `snprintf` to write a cursor position report into a 32-byte string buffer. `snprintf` returns the number of bytes that *would* have been written. If the coordinates are large enough to exceed 31 bytes (e.g., if uninitialized or maliciously sent), `n` will be $>32$. The subsequent `do_process_write(std::string_view(response.data(), static_cast<size_t>(n)))` will read past the end of the 32-byte buffer.
   - **Suggested fix**: `std::string response = "\x1B[" + std::to_string(vt_.row + 1) + ";" + std::to_string(vt_.col + 1) + "R";`

3. **`App::close_dead_panes` Workspace Invalidation Logic Error**
   - **File and Line Number**: `app/app.cpp`, Line 1166
   - **Severity**: HIGH (Logic Error/Potential Crash)
   - **What goes wrong**: The loop iterates over a list of `dead` LeafIds for the *active* workspace. If closing a dead pane causes the workspace itself to close (e.g., the last pane in a tab), `close_workspace()` is called, which activates a *new* workspace. The loop then continues and calls `active_host_manager().close_leaf(id)` using an ID from the *old* workspace on the *newly active* workspace's manager.
   - **Suggested fix**: Break the loop early if a workspace is closed, or collect all dead panes across all workspaces first and handle them per-workspace.

4. **`UnixPtyProcess` Race Condition on `on_output_available_`**
   - **File and Line Number**: `libs/draxul-host/src/unix_pty_process.cpp`, Lines 87 & 124
   - **Severity**: HIGH (Concurrency Race)
   - **What goes wrong**: `on_output_available_` is a `std::function` that is read from the reader thread and written to in `spawn()`. Since `shutdown()` offloads cleanup to a detached thread and returns immediately, a subsequent `spawn()` call can overwrite `on_output_available_` while the *old* reader thread is still running and potentially calling it.
   - **Suggested fix**: Guard `on_output_available_` with `output_mutex_` or join the reader thread synchronously.

### MEDIUM: Edge-Case Failures & Logic Gaps

5. **`NvimProcess::read` (Windows) 32-bit truncation**
   - **File and Line Number**: `libs/draxul-nvim/src/nvim_process.cpp`, Line 186
   - **Severity**: MEDIUM (Logic Error)
   - **What goes wrong**: `bytes_read` is a `DWORD` (unsigned 32-bit). If the read returns more than `INT_MAX` bytes (2GB), the function returns -1 (error), even though the read succeeded. While unlikely for RPC, it's a fragile pattern.
   - **Suggested fix**: Return `static_cast<int>(bytes_read)` directly and ensure `max_len` is capped at `INT_MAX`.

6. **`GlyphCache::rasterize_cluster` Signed Integer Overflow**
   - **File and Line Number**: `libs/draxul-font/src/glyph_cache.cpp`, Line 248
   - **Severity**: MEDIUM (Undefined Behavior)
   - **What goes wrong**: `cluster_height = bbox_top - bbox_bottom`. If `bbox_top` is `INT_MAX` and `bbox_bottom` is negative, this overflows. For extreme or malicious font metrics, this could lead to incorrect allocation sizes.
   - **Suggested fix**: Cast to `int64_t` for intermediate calculations or use `std::clamp` on metrics.

7. **`TerminalHostBase::handle_esc('D'/'M')` Cursor Bounds Logic Error**
   - **File and Line Number**: `libs/draxul-host/src/terminal_host_base_csi.cpp`, Lines 69 & 88
   - **Severity**: MEDIUM (Logic Error)
   - **What goes wrong**: When handling `IND` (Index) or `RI` (Reverse Index), if the cursor is outside the scroll margins (e.g., below `scroll_bottom`), the cursor still moves. Standard VT behavior usually limits these movements to the scroll region or handles them differently when outside.
   - **Suggested fix**: Verify if `vt_.row` is within `[vt_.scroll_top, vt_.scroll_bottom]` before applying scroll logic.

8. **`ConPtyProcess::is_running` side effect without lock**
   - **File and Line Number**: `libs/draxul-host/src/conpty_process.cpp`, Line 193
   - **Severity**: MEDIUM (Concurrency)
   - **What goes wrong**: The `is_running()` method is `const` but modifies `last_exit_code_` (a `mutable` member). If multiple threads call `is_running()` (e.g., main thread and a diagnostic worker), there's a data race on the `std::optional`.
   - **Suggested fix**: Guard the update with a mutex or use an atomic for the exit code.

9. **`RuntimePerfCollectorImpl` Mutex Contention**
   - **File and Line Number**: `libs/draxul-types/src/perf_timing.cpp`, Line 140
   - **Severity**: MEDIUM (Performance/Correctness)
   - **What goes wrong**: The global `RuntimePerfCollector` uses a single mutex for every `report_timing` call. Since `PERF_MEASURE()` is used extensively throughout the codebase, including high-traffic reader threads and the main loop, this creates a massive bottleneck that can skew performance results or even cause stalls in a multi-threaded environment.
   - **Suggested fix**: Use thread-local accumulators for timing data and merge them into the global collector at the end of a frame.
