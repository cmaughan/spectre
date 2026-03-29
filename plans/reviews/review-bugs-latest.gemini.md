I will begin by listing the files in the key directories to map out the codebase and identify high-risk areas such as resource management, threading, and platform-specific implementations.
I'll start with a targeted search for high-risk patterns—specifically thread synchronization, process management, and memory operations—to pinpoint potential race conditions, resource leaks, or unchecked system calls.
I'll examine the process handling in `unix_pty_process.cpp` and `conpty_process.cpp` for platform-specific bugs, then scan for float equality comparisons to identify potential precision issues.
I'll now examine `mpack_codec.cpp` and `rpc.cpp` for potential msgpack decoding flaws, followed by a check of `vk_renderer.cpp` to ensure robust Vulkan resource management.
I've identified potential memory leaks in RPC timeout handling and unchecked `EINTR` in PTY operations. I'll now audit `vk_renderer.cpp` for resource management and `terminal_host_base.cpp` for terminal emulation logic errors.
I've identified several bugs, including a DoS vulnerability in msgpack decoding, memory leaks in RPC, and `EINTR` handling issues in PTY operations. I'll now check `alt_screen_manager.cpp` to see how it handles grid resizing and if there are any further logic errors there.
I've noted a potential logic error in `AltScreenManager::leave` related to grid resizing, but it seems handled by `resize_snapshot`. However, I've identified a possible crash in `terminal_host_base_csi.cpp` and will now audit its CSI parameter parsing for out-of-bounds access.
I've identified several critical and high-severity bugs, including a DoS vulnerability in msgpack decoding, memory leaks in RPC, and potential buffer overruns. I'll now check the `NvimProcess::read` implementation in `nvim_process.cpp` to see if the reader thread could block the UI during shutdown.
Based on a comprehensive review of the Draxul codebase across the `app/`, `libs/`, `shaders/`, and `tests/` directories, I have identified the following bugs and correctness issues.

### CRITICAL SEVERITY

#### 1. Untrusted `reserve()` calls in msgpack decoding (Remote OOM/DoS)
- **File and line number**: `libs/draxul-nvim/src/mpack_codec.cpp:131`, `147`
- **What goes wrong**: The decoder reads array and map counts directly from the msgpack-RPC stream and immediately calls `items.reserve(count)`. A malicious or corrupted msgpack message can specify a very large `count` (e.g., `UINT32_MAX`), causing the application to attempt to allocate gigabytes of memory. This leads to an immediate crash via `std::bad_alloc` or system-wide memory exhaustion (OOM).
- **Suggested fix**: Use a bounded reserve or validate the count against a reasonable maximum before allocating: `if (count > MAX_RPC_ARRAY_SIZE) return val;`.

#### 2. Memory Leak in RPC Request Timeout
- **File and line number**: `libs/draxul-nvim/src/rpc.cpp:143`, `186`
- **What goes wrong**: When an RPC request times out in `request()`, the `msgid` is erased from the `responses_` map. However, if the response eventually arrives late from Neovim, `dispatch_rpc_response` will re-insert the response into the map. Since the original caller has already timed out and removed its entry, this response object (which may contain large amounts of data) will stay in the `responses_` map forever, leaking memory.
- **Suggested fix**: Maintain a set of timed-out/cancelled `msgids` or use a tombstone in the `responses_` map to allow `dispatch_rpc_response` to discard late-arriving responses.

---

### HIGH SEVERITY

#### 3. Reader thread exits prematurely on `EINTR` (Unix PTY)
- **File and line number**: `libs/draxul-host/src/unix_pty_process.cpp:257`
- **What goes wrong**: The terminal reader thread's `poll()` call can be interrupted by a signal (e.g., `SIGCHLD` or `SIGWINCH`). If `poll()` returns `-1` with `errno == EINTR`, the reader thread breaks its loop and exits. This permanently silences all output from the terminal host (Bash/Zsh/etc.) until the process is restarted.
- **Suggested fix**: Wrap the `poll()` call in a loop that continues on `EINTR`.

#### 4. Terminal `write` fails silently on `EINTR`
- **File and line number**: `libs/draxul-host/src/unix_pty_process.cpp:227`
- **What goes wrong**: The `::write()` call to the PTY master FD can be interrupted by a signal. If it returns `-1` with `errno == EINTR`, the function returns `false` and the input (e.g., a keypress or a paste operation) is lost without being retried.
- **Suggested fix**: Wrap the `::write()` call in a loop to handle `EINTR`.

#### 5. RPC Reader Thread blocks UI during shutdown
- **File and line number**: `libs/draxul-nvim/src/rpc.cpp:280`, `libs/draxul-nvim/src/nvim_process.cpp:338`
- **What goes wrong**: `NvimRpc`'s reader thread performs a blocking `read()` on the nvim stdout pipe. During `shutdown()`, the main thread sets `running_` to `false` and calls `join()`. If Neovim is hung or hasn't produced output, the reader thread remains blocked in `read()`, causing the main UI thread to hang indefinitely during exit.
- **Suggested fix**: Use `poll()` with a timeout or a shutdown pipe to allow the reader thread to periodically check the `running_` flag and exit gracefully.

#### 6. Potential Buffer Overrun in `csi_dsr` (Read past end)
- **File and line number**: `libs/draxul-host/src/terminal_host_base_csi.cpp:322`
- **What goes wrong**: `snprintf` is used to write a cursor position report into a 32-byte string buffer. If the output exceeds 31 characters (e.g., very large coordinate values in a corner case), `snprintf` returns the number of characters that *would* have been written. The code then creates a `std::string_view` using this length `n`. If `n >= 32`, the `string_view` will read past the end of the allocated string buffer.
- **Suggested fix**: Check that `n < response.size()` before creating the `string_view`, or use `std::to_chars` for a safer conversion.

---

### MEDIUM SEVERITY

#### 7. Race condition in `grid_handles_` access
- **File and line number**: `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:33`, `81`
- **What goes wrong**: `VkGridHandle` objects are added to and removed from `renderer_.grid_handles_` in their constructors and destructors. This list is iterated in `upload_dirty_state()` on the main thread. If grid handles are managed or hosts are initialized/destroyed on a background thread, the `std::vector` will be mutated while being iterated, causing a crash or iterator invalidation.
- **Suggested fix**: Protect access to the `grid_handles_` vector with a `std::mutex`.

#### 8. Double-close and potential use-after-close of `master_fd_`
- **File and line number**: `libs/draxul-host/src/unix_pty_process.cpp:175`, `250`
- **What goes wrong**: `UnixPtyProcess::shutdown()` closes `master_fd_` before joining the reader thread. The reader thread may simultaneously be calling `poll()` or `read()` on that same FD. Closing an FD being used by another thread in a blocking system call is undefined/unsafe on many platforms and can lead to race conditions where the FD is reused by another part of the app before the reader thread exits.
- **Suggested fix**: Signal the reader thread via the shutdown pipe and `join()` it *before* closing the `master_fd_`.

#### 9. UI Stutter during Process Shutdown
- **File and line number**: `libs/draxul-nvim/src/nvim_process.cpp:326`, `libs/draxul-host/src/unix_pty_process.cpp:152`
- **What goes wrong**: The `shutdown()` implementation for both Neovim and PTY processes includes a blocking `waitpid` loop with a 500ms timeout on the main thread. If a child process is slow to respond to `SIGTERM`, the UI will freeze for up to half a second.
- **Suggested fix**: Move process reaping to a background thread or perform a single `WNOHANG` check per frame in the app loop.

#### 10. Narrowing conversion loss in `mpack_type_ext`
- **File and line number**: `libs/draxul-nvim/src/mpack_codec.cpp:115`
- **What goes wrong**: When reading an extension value, if the length is $\leq 8$, it packs the bytes into an `int64_t`. However, the loop `ext_val = (ext_val << 8) | (uint8_t)ext_data[i]` might overflow if the data is intended as an unsigned 64-bit value or if the extension type expects different packing, and it silently truncates if `len > 8`.
- **Suggested fix**: Add a log warning or handle `len > 8` by storing the raw bytes in `MpackValue` instead of an `int64_t`.
