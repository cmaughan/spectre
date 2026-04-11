All files written. Here's a summary of what was produced:

---

## Results

**Consensus document:** `plans/reviews/review-bugs-consensus.md`

**9 confirmed bugs** (2 CRITICAL, 4 HIGH, 3 MEDIUM) and **6 false positives** identified.

### CRITICAL
| # | Bug | File | Work Item |
|---|-----|------|-----------|
| BUG-01 | `try_get_int`: uncaught `std::terminate` + signed overflow UB | `ui_events.cpp:53` | `00 try-get-int-ub-crash -bug.md` |

### HIGH
| # | Bug | File | Work Item |
|---|-----|------|-----------|
| BUG-02 | Spurious `on_notification_available` on write failure | `rpc.cpp:274` | `01 rpc-spurious-notification-callback -bug.md` |
| BUG-03 | `UnixPtyProcess` use-after-free in detached reaper | `unix_pty_process.cpp:139` | `02 unix-pty-process-reaper-uaf -bug.md` |
| BUG-04 | `FontResolver::initialize()` stale bold/italic flags on reload | `font_resolver.cpp:147` | `03 font-resolver-stale-variant-flags -bug.md` |
| BUG-05 | Non-async-signal-safe heap allocs in fork child | `unix_pty_process.cpp:117`, `nvim_process.cpp:350` | `04 fork-child-async-signal-unsafe -bug.md` |
| BUG-06 | `ConPtyProcess::write()` silent short write (Windows) | `conpty_process.cpp:259` | `05 conpty-short-write -bug.md` |

### MEDIUM
| # | Bug | File | Work Item |
|---|-----|------|-----------|
| BUG-07 | FD close-loop fallback limit 1024 | `nvim_process.cpp:328`, `unix_pty_process.cpp:95` | `06 fd-close-loop-limit -bug.md` |
| BUG-08 | Latent signed overflow in cursor index math | `renderer_state.cpp:242`, `grid.cpp:481` | `07 cursor-index-signed-overflow -bug.md` |
| BUG-09 | `size_t → uint32_t` truncation in mpack encoder | `mpack_codec.cpp:79` | `08 mpack-codec-size-truncation -bug.md` |

### Key interdependencies flagged
- **BUG-03 → BUG-05 → BUG-07**: All three touch the `fork()` child paths in `unix_pty_process.cpp` / `nvim_process.cpp`. Fix BUG-03 first (lifetime model), then restructure `spawn()` for BUG-05, addressing BUG-07 in the same pass.

### False positives
`close_dead_panes` loop (gemini) and `csi_dsr` buffer overrun (gemini) are the most notable — both were claimed as HIGH but are actually defended by existing invariants or early returns.
