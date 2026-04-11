Now I have enough data to write a precise, verified bug report. Here is the full audit:

---

## Draxul Bug Audit Report

All findings below are based on direct source-file reads. Only bugs with a concrete trigger scenario are reported.

---

### CRITICAL

---

#### 1. Unhandled `std::range_error` from `try_get_int` terminates the process

**File:** `libs/draxul-nvim/src/ui_events.cpp:53`
**Severity:** CRITICAL (crash / `std::terminate`)

```cpp
bool try_get_int(const MpackValue& value, int& out)
{
    if (value.type() != MpackValue::Int && value.type() != MpackValue::UInt)
        return false;
    out = (int)value.as_int();   // ← throws if UInt value > INT64_MAX
    return true;
}
```

`as_int()` (`nvim_rpc.h:108–109`) throws `std::range_error` when a `UInt` variant holds a value > `INT64_MAX`. `try_get_int` is called by `handle_grid_line`, `handle_grid_scroll`, `handle_grid_resize`, `handle_grid_cursor_goto`, and `handle_hl_attr_define`—all of which run on the **main thread** via `NvimHost::pump()`. There is no `try/catch` anywhere on the `pump_once → walk_pump → host::pump → process_redraw` call path, so the exception propagates to `std::terminate`.

This asymmetry is telling: the reader thread *is* guarded by `try/catch` (`rpc.cpp:521`), which catches exactly this kind of variant access error, but the main-thread consumer has no equivalent protection.

**Trigger:** A msgpack `uint64` field with value > `0x7FFF FFFF FFFF FFFF` in any grid event (grid coordinates, hl_attr_define ID field, etc.). Possible from a malformed or adversarial nvim process.

**Suggested fix:**
```cpp
bool try_get_int(const MpackValue& value, int& out)
{
    if (value.type() != MpackValue::Int && value.type() != MpackValue::UInt)
        return false;
    try {
        out = (int)value.as_int();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}
```

---

#### 2. Undefined behavior: signed overflow in `try_get_int` narrowing cast

**File:** `libs/draxul-nvim/src/ui_events.cpp:53`
**Severity:** CRITICAL (UB, compiler may miscompile)

The same line `out = (int)value.as_int()` performs a narrowing `int64_t → int` cast. If the int64 value is outside `[-2^31, 2^31-1]` the cast is **signed integer overflow — UB** in C++. Compilers are permitted to eliminate or misbehave on any code that follows such an expression.

The path through `handle_hl_attr_define` shows the problem clearly:
```cpp
int raw_id_int = 0;
if (!try_get_int(args_array[0], raw_id_int))   // UB if attr_id > INT_MAX
    return;
auto raw_id = static_cast<int64_t>(raw_id_int); // already corrupted
if (raw_id < 0 || raw_id > kMaxAttrId)          // too late
```

**Trigger:** Any int64 or uint64 field > `2 147 483 647` in a redraw event. Nvim currently caps attr IDs at 65 535, but there is no protocol-level guarantee, and any future extension or plugin could produce a larger value.

**Suggested fix:** Replace with a range-checked conversion:
```cpp
int64_t v = value.as_int();                   // (inside the existing try/catch above)
if (v < INT_MIN || v > INT_MAX) return false;
out = static_cast<int>(v);
```

---

### HIGH

---

#### 3. Spurious `on_notification_available` callback on write failure in `notify()`

**File:** `libs/draxul-nvim/src/rpc.cpp:274–275`
**Severity:** HIGH (incorrect behavior / violated API contract)

```cpp
if (!impl_->process_->write(...))
{
    DRAXUL_LOG_ERROR(...);
    impl_->read_failed_ = true;
    impl_->response_cv_.notify_all();
    if (callbacks_.on_notification_available)
        callbacks_.on_notification_available();  // ← BUG: no notification was queued
}
```

The `on_notification_available` callback contract (documented in `nvim_rpc.h:188`) states it fires "whenever a new notification is pushed to the queue **or** when the reader thread detects a fatal pipe error." The write-failure path in `notify()` is neither. No notification was pushed, and this isn't the reader thread. The main loop (awakened by `wake_window()`) calls `drain_notifications()`, gets an empty result, and loops—potentially for every keystroke or scroll event that triggers a notify during the connection-failure window.

The same pattern correctly fires the callback on reader-thread pipe errors (`rpc.cpp:435–436`, `rpc.cpp:464–466`, etc.). The `notify()` failure path was added later and lacks the same reasoning.

**Trigger:** Any write to nvim's stdin pipe fails—broken pipe, child exited unexpectedly, etc.

**Suggested fix:** Remove lines 274–275 from the `notify()` write-failure path. The `read_failed_` flag and `response_cv_.notify_all()` are sufficient to unblock any pending `request()` callers; the main loop detects `connection_failed()` on the next pump.

---

#### 4. `dispatch_rpc_response` copies raw `MpackValue` references without validating types

**File:** `libs/draxul-nvim/src/rpc.cpp:324–325`
**Severity:** HIGH (silent data corruption, deferred crash)

```cpp
RpcResponse resp;
resp.msgid = msgid;
resp.error = msg_array[2];   // ← copied by value, but type unchecked
resp.result = msg_array[3];  // ← same
```

`msg_array` contains the decoded values for the entire RPC response packet. If the nvim protocol is violated (e.g., error field is an int, result field is nil for a failure) and the caller then calls `resp.error.as_str()` in `stringify_rpc_error`, that throws `std::bad_variant_access`. This exception IS caught in the reader thread's `try/catch`. However, it increments `malformed_packet_count_` and, if triggered enough times, kills the entire transport—even though the packet may be structurally valid per the RPC spec (just using integer error codes rather than string errors).

The real fix isn't to guard `dispatch_rpc_response`, but to ensure `stringify_rpc_error` is the only path that touches the error field, which it is. No issue.

*Retraction — this is already handled by `stringify_rpc_error`'s defensive code and the outer `try/catch`.* Removing from the report.

---

#### 5. `NvimProcess::spawn()` (macOS): exec-status pipe read end not set `FD_CLOEXEC`

**File:** `libs/draxul-nvim/src/nvim_process.cpp:274`
**Severity:** HIGH (FD leak into spawned child)

`FD_CLOEXEC` is set only on `exec_status_pipe[1]` (the write end, so the kernel closes it in the child on `execvp`). The read end `exec_status_pipe[0]` is explicitly closed in the child by line 307:
```cpp
close(exec_status_pipe[0]);
```

This is correct and deliberate. The parent reads from [0] and the child closes it immediately. No bug; retracting.

---

#### 6. macOS `fork()` child: bulk `close(fd)` loop leaks open signal before final `execvp`

**File:** `libs/draxul-nvim/src/nvim_process.cpp:328–336`
**Severity:** MEDIUM (platform-specific correctness on macOS)

The close loop in the child:
```cpp
const int max_fd = static_cast<int>(sysconf(_SC_OPEN_MAX));
const int limit = (max_fd > 0) ? max_fd : 1024;
for (int fd = STDERR_FILENO + 1; fd < limit; ++fd)
{
    if (fd == exec_status_pipe[1])
        continue;
    close(fd);
}
```

On macOS `sysconf(_SC_OPEN_MAX)` returns `2560` or larger (process rlimit). The loop iterates all candidate FD slots, calling `close()` on each. `close()` on an FD that is not open returns `EBADF`, which is ignored here. The issue: the loop does NOT protect against `EINTR` from a signal handler re-opening FDs between iterations. However, since we called `signal(SIGPIPE, SIG_DFL)` at line 340, and no other signals are unblocked that touch FDs, this is an acceptable risk in practice but could become a correctness issue if signal handling changes.

More practically: `sysconf(_SC_OPEN_MAX)` can be `-1` on some configurations, and the fallback `limit = 1024` may miss FDs opened by library code above that threshold. The correct POSIX approach is to use `closefrom(STDERR_FILENO + 1)` (available on macOS 10.12+) or `close_range` (Linux 5.9+) with a fallback.

**Trigger:** Any library that opens FDs with numbers > 1023 (e.g., certain SSL libraries, large font caches) will leak those FDs into the nvim child if the limit fallback is used.

**Suggested fix:**
```cpp
#ifdef __APPLE__
closefrom(STDERR_FILENO + 1); // except exec_status_pipe[1]
// re-close exec_status_pipe[1] manually only after closefrom
#else
// existing loop
#endif
```
Or, use `/proc/self/fd` enumeration (Linux) / `F_CLOSEM` (BSD). Either way, save and re-open `exec_status_pipe[1]` before the loop, or skip it during.

---

### MEDIUM

---

#### 7. `Grid::mark_dirty_index`: signed `int` cast of `cells_.size()` is UB if grid exceeds `INT_MAX` cells

**File:** `libs/draxul-grid/src/grid.cpp:481`
**Severity:** MEDIUM

```cpp
void Grid::mark_dirty_index(int index)
{
    if (index < 0 || index >= (int)cells_.size())   // ← UB if cells_.size() > INT_MAX
        return;
```

`(int)cells_.size()` is a narrowing cast from `size_t` to `int`. If `cells_.size()` exceeds `INT_MAX` (~2.1 billion), the cast produces an implementation-defined value or UB, potentially making the bounds check always pass and allowing the subsequent array access to go out-of-bounds.

`kMaxGridDim = 10000`, giving a maximum of `10000 × 10000 = 100 000 000` cells, which is safely below `INT_MAX`. The current invariant holds. However, if `kMaxGridDim` is ever raised above ~46 340 (√INT_MAX), or any other code path bypasses the `kMaxGridDim` guard, this becomes a real OOB write.

**Suggested fix:**
```cpp
if (index < 0 || static_cast<size_t>(index) >= cells_.size())
    return;
```

---

#### 8. `RendererState::restore_cursor` / `apply_cursor`: signed overflow in index computation

**File:** `libs/draxul-renderer/src/renderer_state.cpp:242` and `259`
**Severity:** MEDIUM

```cpp
int idx = cursor_row_ * grid_cols_ + cursor_col_;
```

Both `cursor_row_` and `grid_cols_` are `int`. With the current `kMaxGridDim = 10000`, the maximum product is `10000 × 10000 = 100 000 000`, safely within `int`. But the multiplication is **signed**, so if either dimension is ever set outside the validated range (e.g., from a host that bypasses grid validation), this overflows as UB and the subsequent `gpu_cells_[(size_t)idx]` index is unpredictable.

**Suggested fix:**
```cpp
const size_t idx = static_cast<size_t>(cursor_row_) * static_cast<size_t>(grid_cols_) + static_cast<size_t>(cursor_col_);
auto& cell = gpu_cells_[idx];
```

---

#### 9. `mpack_codec.cpp`: silent truncation of string/array length in `write_value`

**File:** `libs/draxul-nvim/src/mpack_codec.cpp:79` and `:82`
**Severity:** MEDIUM

```cpp
mpack_write_str(writer, val.as_str().c_str(), (uint32_t)val.as_str().size());
mpack_start_array(writer, (uint32_t)val.as_array().size());
mpack_start_map(writer, (uint32_t)val.as_map().size());
```

All three casts from `size_t` to `uint32_t` are silent truncations. For a string or container with more than `4 294 967 295` bytes, the length written to the wire would be `real_size % 2^32`, producing a frame-length mismatch that corrupts all subsequent RPC messages for the session.

In practice, nvim strings are bounded to `kMaxMpackStringLen = 64 MB` during decoding, so inbound strings can never be this large. Outbound strings constructed internally (e.g., Lua snippets in `dispatch_action`) also fit comfortably. Nevertheless, this is an unguarded narrowing conversion on an encoding hot path.

**Suggested fix:** Assert or clamp before the cast:
```cpp
assert(val.as_str().size() <= UINT32_MAX);
mpack_write_str(writer, val.as_str().c_str(), static_cast<uint32_t>(val.as_str().size()));
```

---

### Summary

| # | Severity | File | Trigger |
|---|----------|------|---------|
| 1 | CRITICAL | `ui_events.cpp:53` | Malformed UInt>INT64_MAX in any grid event → `std::terminate` |
| 2 | CRITICAL | `ui_events.cpp:53` | Any grid integer > INT_MAX → signed overflow UB |
| 3 | HIGH | `rpc.cpp:274–275` | Notify write failure → spurious wake-up / broken callback contract |
| 6 | MEDIUM | `nvim_process.cpp:328` | Library opens FDs > 1023 → leaked into nvim child (macOS fallback path) |
| 7 | MEDIUM | `grid.cpp:481` | `cells_.size() > INT_MAX` → UB bounds check (currently guarded by `kMaxGridDim`) |
| 8 | MEDIUM | `renderer_state.cpp:242,259` | Signed integer overflow in cursor index multiply if dims escape validation |
| 9 | MEDIUM | `mpack_codec.cpp:79,82` | Outbound string/array > 4 GB → silent length truncation corrupts RPC stream |

The two CRITICAL issues (#1 and #2) share the same root cause and should be fixed together with a single range-checked `try_get_int` replacement.
