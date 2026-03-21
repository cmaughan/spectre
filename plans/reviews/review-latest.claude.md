# Draxul Codebase Review Report

*Generated: 2026-03-21 | Codebase: ~19,000 lines of C++*

---

## Executive Summary

Draxul is a Neovim GUI frontend with a well-structured library decomposition, clean GPU abstraction layers, and solid platform support (Metal/macOS, Vulkan/Windows). The architecture is sound, but shows organic growth patterns: proliferating callbacks, mixed concerns in App, and insufficient input validation. The most critical risks are threading races, unbounded memory growth in the VT parser, and missing bounds checks on RPC-supplied coordinates.

---

## Detailed Findings

### 1. Code Quality Issues

#### 1.1 Namespace Pollution & Abbreviated Naming
- **Location:** Throughout codebase (e.g., `libs/draxul-font/src/text_service.cpp:13–21`)
- Struct names like `Impl`, variables like `cw`/`ch`, `msg_`, `cbs_` — opaque to new contributors.

#### 1.2 Renderer Backend Code Duplication
- **Location:** `libs/draxul-renderer/src/vulkan/vk_renderer.h:40–50` vs. `libs/draxul-renderer/src/metal/metal_renderer.h:46–62`
- Nearly identical method signatures across VkRenderer and MetalRenderer with no shared base implementation. Bug fixes require touching both codepaths.

#### 1.3 Unsafe `void*` Bridge for Metal Objects
- **Location:** `libs/draxul-renderer/src/metal/metal_renderer.h:83–94`
- ObjC++ Metal objects held as `void*` in C++ compilation units. Incorrect casts are invisible to the compiler.

#### 1.4 Unbounded String Accumulation in VtParser
- **Location:** `libs/draxul-host/src/vt_parser.cpp:62–65`
- `plain_text_`, `csi_buffer_`, `osc_buffer_` have no max size. A pathological or buggy Nvim stream causes unbounded memory growth.

#### 1.5 Raw Pointer Aliasing Without Lifetime Guarantees
- **Location:** `libs/draxul-host/src/grid_host_base.cpp:11–25`
- `window_`, `renderer_`, `text_service_` stored as raw pointers with no lifetime enforcement. Use-after-free if App shuts down out-of-order.

#### 1.6 Magic Numbers
- **Location:** `libs/draxul-nvim/src/nvim_process.cpp:142` — `WaitForSingleObject(..., 2000)` (2 s hardcoded)
- **Location:** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:142` — `80 * 24 * sizeof(GpuCell)` hardcoded

#### 1.7 No Input Validation in UI Event Handler
- **Location:** `libs/draxul-nvim/src/ui_events.cpp:187–200`
- `handle_grid_line()` extracts row/col_start from msgpack with no bounds check against grid dimensions.

#### 1.8 Grid Scroll Warnings Without Release Handling
- **Location:** `libs/draxul-grid/src/grid.cpp:113–128`
- Out-of-bounds scroll region logged as WARN + `assert()`. In Release builds the assert vanishes; undefined behaviour follows.

#### 1.9 Integer Overflow in Grid Indexing
- **Location:** `libs/draxul-grid/src/grid.cpp:54` — `int index = row * cols_ + col`
- `cols_` from a malformed RPC resize could overflow a signed int. Should use `size_t` with bounds guard.

#### 1.10 Descriptor Counts Hardcoded in Vulkan Pipeline
- **Location:** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:232–240`
- `VkDescriptorPoolSize` entries are hardcoded constants; pipeline changes require manual sync.

---

### 2. Testing Gaps

#### 2.1 GPU Resource Cleanup Untested
- `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:166–189` — `shutdown()` is never validated for idempotency or full resource release.

#### 2.2 Process Spawn Failure Paths
- `libs/draxul-nvim/src/nvim_process.cpp:78–150` — 7+ early-exit points (pipe creation failures, `CreateProcessA` failure); test coverage unlikely.

#### 2.3 DPI Hotplug During Active Frame
- `on_display_scale_changed()` → TextService rebuild → glyph re-shape chain has no test validating frames don't crash mid-rebuild.

#### 2.4 Atlas Exhaustion Recovery
- `libs/draxul-font/src/text_service.cpp:179–182` — `consume_atlas_reset()` is called but no test exercises behaviour under sustained high-glyph-rate load.

#### 2.5 Grid Scroll Edge Cases
- `libs/draxul-grid/src/grid.cpp:105–231` — complex double-width char repair logic in scrolling is undertested at boundaries.

#### 2.6 RPC Backpressure Under Load
- No test simulates rapid redraw from Nvim while App is blocked on a GPU frame.

#### 2.7 Shutdown Race Condition
- Host thread servicing RPC queue vs. `App::shutdown()` — timing-dependent; no deterministic test.

#### 2.8 No Fake Renderer for Headless Unit Tests
- Every test that touches rendering requires a real GPU driver. No stub/fake renderer abstraction exists.

#### 2.9 Cursor Blink State Machine
- Cursor visibility depends on blink timing, busy state, and deadline. Complex state with no coverage of transition edge cases.

#### 2.10 File Drop Path Validation
- `app/input_dispatcher.cpp:111–114` — `open_file:` action string with colon-separated path; no test for paths containing colons or special characters.

---

### 3. Separation of Concerns Violations

#### 3.1 App Layer Owns Too Much (567 lines)
- `app/app.cpp` orchestrates: window lifecycle, renderer init, font loading, host creation, input dispatch, diagnostics panel, config persistence — all in one file.

#### 3.2 AppConfig Leaks SDL Knowledge
- `libs/draxul-app-support/include/draxul/app_config.h:20–26` — `GuiKeybinding::key` is typed as SDL_Keycode (a comment says so); the support library has an implicit SDL dependency.

#### 3.3 GridHostBase Mixes Unrelated Concerns
- `libs/draxul-host/src/grid_host_base.cpp` — manages grid state, cursor blinking, text input area, highlight table, rendering pipeline, and viewport math all in one base class.

#### 3.4 HostManager Post-Init Dynamic Cast
- `app/host_manager.cpp:21–82` — `dynamic_cast<I3DHost*>` after initialization attaches the renderer; implicit coupling that breaks if host lifecycle changes.

#### 3.5 Generic `dispatch_action()` String Interface
- `libs/draxul-host/include/draxul/host.h:99` — hosts accept arbitrary `std::string_view action`; typos silently do nothing, no compile-time type safety.

---

### 4. Threading & Synchronization Issues

#### 4.1 Race in UiRequestWorker
- `libs/draxul-app-support/include/draxul/ui_request_worker.h:24–28` — `rpc_` pointer read by worker thread while main thread may be setting it or calling `stop()`. No synchronization visible.

#### 4.2 No Enforcement That Grid Is Main-Thread Only
- `libs/draxul-grid/src/grid.cpp:28–50` — Grid is mutated by the main thread, but no `DRAXUL_ASSERT_MAIN_THREAD()` macro prevents accidental access from a background thread.

#### 4.3 Reader Thread Reads Closed FD
- POSIX NvimProcess — `shutdown()` closes `child_stdout_read_` without signaling the reader thread. Reader may call `read()` on a closed fd (EBADF) depending on timing.

---

### 5. Memory Management Issues

#### 5.1 Pimpl Move Semantics Unclear
- `libs/draxul-font/src/text_service.cpp:71–80` — `= default` move is used but Impl's own move-correctness is unverified; non-copyable resources could double-free or leak.

#### 5.2 Shared Pointer Ownership Not Documented
- `libs/draxul-renderer/src/vulkan/vk_renderer.h:115` — `shared_ptr<IRenderPass>` ownership transfer semantics at `register_render_pass()` vs. `unregister_render_pass()` are undocumented.

---

### 6. Architecture Issues

#### 6.1 Polling Loop Instead of Event-Driven
- `app/app.cpp:282–286` — `run()` is `while(running_) { pump_once(); }`. RPC notifications are polled each frame; no reactive wakeup mechanism.

#### 6.2 No Configuration Validation
- `libs/draxul-app-support/include/draxul/app_config.h:26–53` — Negative window sizes, font sizes below minimum, etc., are never rejected at parse/apply time.

#### 6.3 Callback Proliferation in GuiActionHandler
- `app/app.cpp:156–173` — `GuiActionHandler::Deps` holds 4+ callback functions with no documented invocation order or error propagation.

---

## Top 10 Good Things

1. **Clean library decomposition** — `draxul-types`, `draxul-window`, `draxul-renderer`, `draxul-font`, `draxul-grid`, `draxul-nvim`, `draxul-host`, `draxul-app-support` are clearly bounded with explicit CMake targets and one-way dependency arrows.

2. **Pimpl pattern used consistently** — All major subsystems (TextService, NvimProcess, SdlWindow) hide implementation behind `Impl` structs, keeping public headers clean and reducing recompile cascades.

3. **Renderer hierarchy is well-abstracted** — `IBaseRenderer → I3DRenderer → IGridRenderer` allows new render passes to be registered without modifying the core pipeline.

4. **IRenderPass / IRenderContext pattern** — Any subsystem can register typed render passes; the renderer calls them each frame without knowing what they do. Excellent for extensibility.

5. **Replay fixture test infrastructure** — `tests/support/replay_fixture.h` enables deterministic RPC/redraw testing without spawning a real Nvim process. The builder pattern is clean and easy to extend.

6. **Font pipeline is thorough** — FreeType → HarfBuzz → GlyphCache → shelf-packed atlas is a correct and complete text rendering pipeline with ligature support.

7. **Render smoke/snapshot test suite** — `do.py bless*` workflows enable per-scenario visual regression testing, which is non-trivial for GPU-rendered applications.

8. **Explicit threading model documented** — Main thread owns grid and GPU; reader thread is strictly a pipe pump. The model is simple, documented, and largely respected in code.

9. **FetchContent for all dependencies** — No vendored source trees; all external libraries (SDL3, FreeType, HarfBuzz, MPack, vk-bootstrap, VMA) are fetched declaratively. Reproducible builds.

10. **Platform presets in CMake** — `mac-debug`, `mac-release`, `mac-asan`, `default`, `release` provide one-command configuration for all common workflows including AddressSanitizer builds.

---

## Top 10 Bad Things

1. **No bounds validation on RPC-supplied grid coordinates** — `handle_grid_line()` trusts Nvim's row/col values without checking against grid dimensions. A malformed message causes out-of-bounds memory access.

2. **Unbounded buffers in VtParser** — `plain_text_`, `csi_buffer_`, `osc_buffer_` grow without limit. Sustained abnormal output triggers memory exhaustion.

3. **Threading race in UiRequestWorker** — The `rpc_` pointer is accessed by a worker thread with no visible synchronization. Concurrent set/stop from the main thread is a data race.

4. **App class owns too much** — 567-line `app.cpp` orchestrates every subsystem. Changes to any one subsystem require reasoning about the whole file; hard to test in isolation.

5. **Generic `dispatch_action(string)` interface** — No compile-time type safety on action names. Typos silently fail; no exhaustiveness check at call sites.

6. **`void*` bridge for Metal objects** — ObjC++ objects are passed between compilation units as `void*`. A single mismatched cast causes a silent runtime crash with no compiler warning.

7. **Reader thread / shutdown race on POSIX** — `shutdown()` closes the read fd without notifying the reader thread. The reader may call `read()` on a closed fd, producing EBADF or worse.

8. **No fake/stub renderer for unit tests** — Every rendering-adjacent test requires a real GPU. This blocks headless CI and slows the test feedback loop.

9. **Magic numbers throughout** — Hardcoded 2000 ms shutdown timeout, `80 * 24` default grid size, and hardcoded descriptor pool counts all need hunting-down when constants change.

10. **Post-init `dynamic_cast` in HostManager** — Attaching the 3D renderer via a `dynamic_cast` after host initialization is implicit coupling. Silent no-op if the cast fails in a new host subclass.

---

## Best 10 Quality-of-Life Features to Add

1. **Session restore** — Remember window position, size, and the last-opened file/directory so the editor returns to exactly where the user left off.

2. **Font fallback chain UI** — A settings panel (or config key) to specify an ordered fallback font list for CJK, emoji, and symbols, rather than relying solely on automatic resolution.

3. **Per-monitor DPI font scaling** — Automatically adjust font size when the window is dragged to a monitor with a different display scale, without requiring a restart.

4. **Live config reload** — Watch `config.toml` for changes and apply updated font size, keybindings, and ligature settings without relaunching.

5. **Diagnostic overlay toggle** — A keybinding-activated overlay showing current FPS, GPU buffer write time, glyph atlas fill percentage, and RPC queue depth — useful for performance tuning.

6. **Multiple window support** — Allow spawning additional Draxul windows sharing one Nvim instance (`nvim --remote`), matching the workflow of GUI Neovim clients like Neovide.

7. **System font picker** — A GUI font picker dialog (or fuzzy search in a command popup) to browse and switch fonts without manually editing `config.toml`.

8. **Smooth scrolling** — Animate `grid_scroll` events over a few frames with configurable easing, matching the UX of other modern terminal emulators.

9. **Cursor animation** — Animate cursor movement across cells (position interpolation) with configurable speed, making large jumps easier to track visually.

10. **macOS native menu bar integration** — Wire common actions (new window, open file, copy/paste, font size) into the macOS menu bar so the application behaves as a first-class macOS citizen.

---

## Best 10 Tests to Add for Stability

1. **`grid_line` out-of-bounds coordinate** — Feed a `grid_line` event with row and col values outside the current grid dimensions; assert no crash, no memory corruption, and a graceful WARN log.

2. **Atlas exhaustion + reset cycle** — Drive enough unique glyphs to exhaust the 2048×2048 atlas, trigger `consume_atlas_reset()`, then verify subsequent renders are correct and no GPU resource leaks.

3. **DPI change mid-frame** — Simulate `on_display_scale_changed()` while a frame render is in-flight; verify no crash, no stale glyph data in the atlas, and correct cell sizing after rebuild.

4. **VtParser buffer overflow** — Feed a stream of 1 MB of CSI sequences; assert `csi_buffer_` does not grow beyond a defined max, drops bytes gracefully, and does not OOM.

5. **NvimProcess shutdown race (POSIX)** — Start the reader thread, then call `shutdown()` on a background thread concurrently; verify no EBADF crash and clean join of the reader thread.

6. **Scroll at grid boundaries with double-width characters** — Scroll a region where the last cell is the left half of a double-width character; verify the orphaned half is correctly repaired to a space.

7. **Rapid font size change** — Trigger `font_increase` / `font_decrease` in quick succession (faster than a full frame); verify no glyph atlas corruption and no use-after-free of the old TextService.

8. **Keybinding conflict detection** — Load a config with two actions mapped to the same key; assert a clear error or last-wins with a WARN log rather than silent incorrect behaviour.

9. **Renderer `shutdown()` idempotency** — Call `shutdown()` twice on both VkRenderer and MetalRenderer; verify no double-free, no Vulkan validation layer errors, and no ObjC++ over-release.

10. **File drop with path containing colons** — Simulate an SDL drop-file event with a path like `/foo:bar/file.txt`; verify the `open_file:` action is parsed correctly and the host receives the full path.

---

## Worst 10 Features (Most Likely to Cause Problems or Least Useful)

1. **`dispatch_action(std::string_view)` generic string bus** — An untyped string channel for inter-module actions is fragile, ungreppable, and untestable. Every call site is a potential typo-induced silent failure.

2. **Hardcoded 80×24 initial grid size** — The default grid buffer sized to `80 * 24 * sizeof(GpuCell)` is a vestigial terminal assumption. It silently under-allocates for large windows until the first resize event.

3. **Windows command-line quoting in `quote_windows_arg()`** — Hand-rolled Windows argument quoting for `CreateProcessA lpCommandLine` is inherently fragile (trailing backslashes, embedded quotes). This is a latent correctness bug and a potential injection surface.

4. **`(void)logical_h` suppression in app.cpp:505** — Explicitly suppressing an unused-variable warning rather than fixing the underlying logic indicates dead or misunderstood code that will confuse maintainers.

5. **HostManager post-init `dynamic_cast` for 3D attachment** — Silently does nothing if a new host subclass forgets to implement `I3DHost`. There is no compile-time enforcement, no runtime error, and no log entry on cast failure.

6. **Cursor blink implemented as deadline arithmetic in GridHostBase** — A timing-based cursor blink buried inside the grid host base class means any latency (slow GPU frame, heavy RPC batch) causes visible blink glitches. Should be decoupled from frame timing.

7. **`#ifdef DRAXUL_ENABLE_RENDER_TESTS` gating in main.cpp** — Large blocks of render-test code that are never compiled in production builds are maintenance debt. They bitrot silently between test runs and increase binary complexity for CI-only paths.

8. **`log_display_info()` debug-only DPI logging** — Called only at `LogLevel::Debug`; DPI-related bugs in production (common on multi-monitor setups) produce no diagnostic output unless the user happens to have debug logging enabled.

9. **AppConfig with no invariant validation** — Config fields like `font_size`, `window_width`, `window_height` accept any value including negatives and zeros. Invalid configs crash at first use rather than at parse time.

10. **`GuiActionHandler::Deps` callback struct with 4+ function pointers** — No documented invocation order, no error propagation from callbacks, and no way to mock individual callbacks without touching the entire struct. Grows unbounded as new actions are added.

---

*End of report.*
