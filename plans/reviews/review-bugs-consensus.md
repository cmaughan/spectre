All files are written. Here's a summary of what was produced:

---

## Consensus complete

**`plans/reviews/review-bugs-consensus.md`** — 19 confirmed bugs, 6 false positives, interdependency map, and fix order.

**19 work items filed in `plans/work-items/` (48–66):**

| # | File | Severity | Bug |
|---|------|----------|-----|
| 48 | megacity-host-renderer-config-data-race | CRITICAL | Data race on `renderer_config_` |
| 49 | bmp-signed-integer-ub | CRITICAL | Signed overflow in write:46 and read_u32:97 |
| 50 | sdl-file-dialog-window-use-after-free | CRITICAL | `SDL_Window*` UAF in async callback |
| 51 | sdl-file-dialog-path-leak-on-push-failure | MEDIUM | Path leaked on `SDL_PushEvent` failure |
| 52 | perf-timing-unsigned-underflow | HIGH | Unsigned wrap corrupts EMA timing display |
| 53 | megacity-render-image-size-wrong | HIGH | `ImageResource::size` stores width not bytes |
| 54 | megacity-render-create-sampled-image-resource-leak | HIGH | VMA/ImageView/Sampler leak on partial failure |
| 55 | megacity-render-upload-mesh-empty-input-vulkan-violation | HIGH | `vkCreateBuffer(size=0)` spec violation |
| 56 | compact-attr-ids-excludes-scrollback | HIGH | Scrollback loses all colors after compaction |
| 57 | scrollback-restore-stale-stride-after-resize | HIGH | Visual corruption after resize+restore |
| 58 | unix-pty-poll-exits-on-eintr | HIGH | Terminal silenced by SIGWINCH/SIGCHLD |
| 59 | unix-pty-write-silent-fail-on-eintr | HIGH | Keypress/paste dropped on interrupted write |
| 60 | mpack-unbounded-reserve-oom | HIGH | OOM crash on large msgpack count |
| 61 | vk-sync-object-partial-create-leak | MEDIUM | Semaphore/fence leak on init failure |
| 62 | glyph-cache-signed-overflow-empty-cluster | MEDIUM | UB on `INT_MIN - INT_MAX` in bbox |
| 63 | unix-pty-shutdown-master-fd-close-ordering | MEDIUM | Close-before-join fd race on shutdown |
| 64 | megacity-host-callbacks-raw-pointer-lifetime | MEDIUM | Raw `callbacks_` pointer UAF on teardown |
| 65 | rpc-late-response-memory-leak | MEDIUM | Late RPC responses accumulate forever |
| 66 | app-ppi-float-equality-relayout | MEDIUM | Float equality misses DPI relayout |

**Key false positives dropped:** Claude BUG-11 (`waitpid` blocking — already fixed with WNOHANG loop), Gemini `csi_dsr` buffer overrun (max 14 chars in 32-byte buffer), Gemini `grid_handles_` race (main-thread only lifecycle).
