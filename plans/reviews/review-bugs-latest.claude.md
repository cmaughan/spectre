All findings confirmed. Here is the complete bug report:

---

# Draxul Bug Report

## BUG-1 — CRITICAL: `conpty_process.cpp` missing trailing-backslash escape before closing quote

**File:** `libs/draxul-host/src/conpty_process.cpp:38`
**Severity:** CRITICAL (command-line injection / process launch failure on Windows)

The `quote_windows_arg` function tracks consecutive backslashes in `backslashes` but never uses that counter before appending the closing `"`. When an argument ends with one or more backslashes (e.g. a working directory like `C:\Users\dev\`), those unescaped backslashes cause the closing `"` to be interpreted as a literal quote by `CommandLineToArgvW`, breaking the entire command line. The identical function in `libs/draxul-nvim/src/nvim_process.cpp:74` correctly has `quoted.append(backslashes, '\\');` before the closing `push_back('"')`. The `conpty_process.cpp` copy is missing that line.

**Trigger:** Any argument passed to a ConPTY-launched process that ends with `\` (e.g. a `working_dir` value).

**Fix:**
```cpp
// conpty_process.cpp line 37, before quoted.push_back('"')
quoted.append(backslashes, '\\');   // <-- add this line
quoted.push_back('"');
```

---

## BUG-2 — HIGH: Signed integer overflow in `CapturedFrame::valid()`

**File:** `libs/draxul-types/include/draxul/types.h:92`
**Severity:** HIGH (undefined behavior, incorrect validation result)

```cpp
return width > 0 && height > 0 && rgba.size() == static_cast<size_t>(width * height * 4);
```

`width * height * 4` is computed as `int * int * int` before the `static_cast<size_t>` is applied. For frames larger than ~23170×23170 the signed multiplication overflows — undefined behavior — so `valid()` can return `true` on a corrupt frame or `false` on a valid one.

**Trigger:** Any render test or screenshot with dimensions whose product exceeds `INT_MAX / 4`.

**Fix:**
```cpp
rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4
```

---

## BUG-3 — HIGH: Signed integer overflow in `read_bmp_rgba` row-offset arithmetic

**File:** `libs/draxul-types/src/bmp.cpp:137–138`
**Severity:** HIGH (undefined behavior, out-of-bounds reads from `bytes[]`)

```cpp
const auto src_row = pixel_offset + static_cast<size_t>(src_y * width * 4);
const auto dst_row = static_cast<size_t>(y * width * 4);
```

Both expressions multiply `int * int * int`. The outer `static_cast<size_t>()` is applied to the already-overflowed result. For a BMP with `width × row_index × 4 > INT_MAX` (e.g. width ≥ 16384 at row ≥ 32768), the row offset wraps into garbage, causing `bytes[src]` to read from an earlier part of the buffer and silently produce wrong pixel data (or an out-of-bounds read if the wrapper value lands below `pixel_offset`).

The bounds check at line 125–126 uses correct `size_t` arithmetic and passes; only the per-row loop uses the broken `int` expression.

**Fix:**
```cpp
const auto src_row = pixel_offset + static_cast<size_t>(src_y) * static_cast<size_t>(width) * 4;
const auto dst_row = static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
```

---

## BUG-4 — MEDIUM: Windows `NvimProcess::write` does not handle partial `WriteFile` results

**File:** `libs/draxul-nvim/src/nvim_process.cpp:187`
**Severity:** MEDIUM (RPC stream corruption)

```cpp
return WriteFile(impl_->child_stdin_write_, data, (DWORD)len, &written, nullptr) && written == len;
```

`WriteFile` to a pipe is allowed to write fewer bytes than requested (partial write), in which case the call returns `TRUE` with `written < len`. The current code returns `false` in that case without retrying, so the tail of the msgpack message is silently dropped. The POSIX implementation (lines 378–386) loops until all bytes are delivered.

**Trigger:** Sending a large RPC request (e.g. pasting many lines) when the pipe's kernel buffer is nearly full.

**Fix:** Wrap in a retry loop advancing the data pointer by `written` bytes each iteration, mirroring the POSIX implementation.

---

## BUG-5 — MEDIUM: `dispatch_rpc_response` / `dispatch_rpc_request` cast `int64_t` message ID to `uint32_t` without validation

**File:** `libs/draxul-nvim/src/rpc.cpp:212, 240`
**Severity:** MEDIUM (response misrouting or silent discard)

```cpp
uint32_t msgid = (uint32_t)msg_array[1].as_int();   // line 212
auto req_msgid = (uint32_t)msg_array[1].as_int();   // line 240
```

`as_int()` returns `int64_t`. A negative or > 2³²−1 value from a corrupted stream truncates silently. A negative ID could collide with a valid in-flight `msgid`, causing the wrong waiting thread to be unblocked with another request's response.

**Trigger:** Corrupted msgpack stream, or a future nvim version that uses 64-bit IDs.

**Fix:** Validate before casting:
```cpp
int64_t raw_id = msg_array[1].as_int();
if (raw_id < 0 || raw_id > UINT32_MAX) { /* log and discard */ return; }
uint32_t msgid = static_cast<uint32_t>(raw_id);
```
