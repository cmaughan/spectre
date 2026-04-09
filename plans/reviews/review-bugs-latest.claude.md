Now I have a thorough picture of the codebase. Let me compile the confirmed bugs.

---

## Draxul Bug Report

### MEDIUM — `libs/draxul-nvim/src/nvim_process.cpp:422` — POSIX write() silently fails on EINTR

**What goes wrong:** `NvimProcess::write()` (POSIX path) does not retry after a signal-interrupted syscall. SDL installs `SIGTERM`/`SIGINT` signal handlers. If any handled signal is delivered while the main thread is mid-write to nvim's stdin pipe, `write()` returns −1 with `errno == EINTR`. The code treats this as a hard error, returns `false`, and `NvimRpc::notify()`/`request()` then sets `read_failed_ = true` and tears down the RPC transport — crashing the session on a spurious signal.

`unix_pty_process.cpp:255` and `:292` correctly handle EINTR in their write loops; `nvim_process.cpp` does not.

**Fix:**
```cpp
ssize_t n = ::write(impl_->child_stdin_write_, data + total_written, len - total_written);
if (n < 0) {
    if (errno == EINTR) continue;   // add this
    return false;
}
if (n == 0) return false;
```

---

### MEDIUM — `libs/draxul-nvim/src/nvim_process.cpp:432` — POSIX read() silently fails on EINTR

**What goes wrong:** `NvimProcess::read()` (POSIX path) makes a single `::read()` call and returns −1 on any error, including `EINTR`. The RPC reader thread calls this in a tight loop; if a signal is delivered while the call blocks, `read()` returns −1, the reader thread logs "nvim pipe read error", sets `read_failed_ = true`, and terminates the transport.

**Fix:**
```cpp
ssize_t n;
do { n = ::read(impl_->child_stdout_read_, buffer, max_len); }
while (n < 0 && errno == EINTR);
if (n < 0) return -1;
return (int)n;
```

---

### MEDIUM — `libs/draxul-types/src/bmp.cpp:126` — `std::abs(INT32_MIN)` is undefined behaviour

**What goes wrong:** `read_bmp_rgba` reads `height` as `int32_t` from an untrusted file (render-test reference BMPs), then computes:
```cpp
const int abs_height = std::abs(height);
```
If the file contains `height = INT32_MIN` (−2147483648), `std::abs(INT32_MIN)` is UB because the mathematical result (2147483648) is not representable in `int32_t`. The existing guard only rejects `height == 0`. In practice this only affects render-test inputs, but it is latent UB that a sanitizer build will flag.

**Fix:** Add a guard before line 126:
```cpp
if (height == INT32_MIN)
    return std::nullopt;
const int abs_height = std::abs(height);
```

---

### MEDIUM — `libs/draxul-nvim/src/nvim_process.cpp:220` (Windows) — `ReadFile` return value narrowing truncates large reads

**What goes wrong:** `NvimProcess::read()` on Windows:
```cpp
DWORD bytes_read;
if (!ReadFile(impl_->child_stdout_read_, buffer, (DWORD)max_len, &bytes_read, nullptr))
    return -1;
return (int)bytes_read;
```
`bytes_read` is `DWORD` (unsigned 32-bit). The return type is `int` (signed 32-bit). If `ReadFile` somehow returns more than `INT_MAX` bytes (impossible on Windows pipes in practice, but the cast is still formally wrong), the result becomes negative and the caller (rpc.cpp) interprets it as a read error. More concretely, if `max_len` is cast from `size_t` to `DWORD` and `max_len > UINT32_MAX`, the `(DWORD)max_len` truncation silently reduces the requested read size with no error.

The read buffer (`impl_->read_buf_`) is 256 KB (line 115 in rpc.cpp), so `(DWORD)max_len` does not truncate in practice. However the return narrowing `(int)bytes_read` is the live footgun: any caller that passes `max_len > INT_MAX` would get a negative return value. The reader thread's `if (n <= 0)` check would then log a read error and tear down the transport.

**Fix:**
```cpp
return (bytes_read > (DWORD)INT_MAX) ? -1 : (int)bytes_read;
```
Or cap `max_len` to `INT_MAX` before the call.

---

### MEDIUM — `libs/draxul-host/src/scrollback_buffer.cpp:29` — Potential integer overflow in resize temporary allocation

**What goes wrong:** In `ScrollbackBuffer::resize()`:
```cpp
std::vector<Cell> tmp((size_t)old_count * cols);
```
`old_count` is `int` (bounded by `kCapacity`) and `cols` is `int` (the new column count). The multiplication `old_count * cols` is performed in **signed int** before the cast to `size_t`. If `kCapacity * cols > INT_MAX` (e.g. `kCapacity = 10000`, `cols = 300000` — unrealistic but not guarded), this signed overflow is UB. The actual clamp on cols comes from the grid which limits cols to `kMaxGridDim = 10000` (grid.cpp:246), but that check lives in the Grid, not in ScrollbackBuffer; there is no corresponding guard in `resize()` itself.

**Concrete trigger:** A malformed/fuzzed `grid_resize` event with cols > 200,000 could reach this code with a signed overflow before the size_t cast.

**Fix:**
```cpp
std::vector<Cell> tmp(static_cast<size_t>(old_count) * static_cast<size_t>(cols));
```

---

### LOW — `libs/draxul-host/src/vt_parser.cpp:168` — OSC escape-string terminator discards non-`\\` character silently

**What goes wrong:** In `State::OscEsc`:
```cpp
case State::OscEsc:
    if (ch == '\\')
        cbs_.on_osc(osc_buffer_);
    state_ = State::Ground;
    break;
```
If the character after `ESC` inside an OSC sequence is not `\\` (e.g. another printable char), the parser transitions to Ground without calling `on_osc` and without re-feeding the non-`\\` character to the Ground handler. The pending character is silently dropped. If that character was the start of a new escape sequence (`[`, `]`, etc.), the following sequence is lost entirely.

**Concrete trigger:** Terminal output containing `\x1B]0;title\x1B[A` (an OSC 0 title followed immediately by a cursor-up CSI without a proper ST terminator) would silently drop the `[A` CSI if OSC parsing consumed the `\x1B` as the start of an ST.

**Fix:** When `ch != '\\'`, push it into `plain_text_` (or re-enter the appropriate CSI/Escape state) instead of discarding:
```cpp
case State::OscEsc:
    if (ch == '\\') {
        cbs_.on_osc(osc_buffer_);
    } else {
        // ESC was consumed as potential ST; re-process ch as if in Escape state.
        if (ch == '[') { csi_buffer_.clear(); state_ = State::Csi; return; }
        else if (ch == ']') { osc_buffer_.clear(); state_ = State::Osc; return; }
        else if (cbs_.on_esc) cbs_.on_esc(ch);
    }
    state_ = State::Ground;
    break;
```
