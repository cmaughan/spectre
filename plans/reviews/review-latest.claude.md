# Draxul Codebase Technical Review

## Executive Summary

Draxul is a GPU-accelerated Neovim GUI frontend with cross-platform rendering (Vulkan/Metal), advanced terminal emulation, and a 3D code visualization (MegaCity). The codebase demonstrates strong architectural discipline, excellent test coverage (87 test files, 27K+ test LOC), and meticulous platform code hygiene. 391 completed work items tracked.

---

## 1. Module Separation & Architecture

### Strengths
- **Clean layering**: Excellent separation between draxul-types (POD), draxul-window (SDL abstraction), draxul-renderer (GPU backends), draxul-font (text pipeline), draxul-grid (terminal grid), and draxul-host.
- **Platform abstraction**: Vulkan vs Metal cleanly separated at compile time via `src/vulkan/` and `src/metal/` directories with no platform ifdefs leaking into shared code.
- **Interface-first design**: Heavy use of pure virtual base classes (`IWindow`, `IGridRenderer`, `IHost`, `IBaseRenderer`) enables testability and loose coupling.
- **Dependency injection pattern**: `AppDeps`, `HostManager::Deps`, `InputDispatcher::Deps` all use struct-based dependency bundles rather than singletons — excellent for testing.
- **No circular dependencies**: Dependency graph is strictly acyclic (types < window < renderer < font < grid < host < app).

### Issues
- **IHost interface is large** (100+ methods including nested callbacks) — could be split into smaller focused interfaces (`IGridHost`, `IInputHandler`, `IDrawable`).
- **`draxul-app-support` is vaguely named**: What "support" means is unclear from the name alone.
- **60+ uses of `std::function`**: Indicates callback/lambda density that could be reduced with static dispatch.
- **HostManager is somewhat large** (~200 lines of header) — manages tree, hosts, viewports, zoom, splitting, renaming. Could split into smaller focused types.

---

## 2. Code Smells

### Critical Issues

1. **Raw pointers in critical paths**: `IWindow*`, `IGridRenderer*`, `TextService*` stored as raw borrowed pointers in `GridHostBase`, `HostManager`, `HostContext`. If App reshuffles ownership, dangling pointers are possible.

2. **Magic numbers scattered**:
   - `kMaxNotificationQueueDepth = 4096`, `kNotificationQueueWarnDepth = 512`
   - Scrollback line count hardcoded in comments rather than named constants
   - Cell buffer size calculations without clear documentation

3. **Long functions**: `App::initialize()` is 200+ lines. `create_host_for_leaf()` similarly long. Not critical (well-structured), but could benefit from helper extractions.

4. **Inconsistent error handling**:
   - Some functions return `bool` with `.error()` method
   - Others use `std::optional` or `RpcResult`
   - Some silently fail (clipboard operations log warning but don't propagate failure)
   - No unified `Result<T, Error>` type

5. **Notification queue backpressure undefined**: Queue reaches 4096 before warning, 512 for early warning. Unclear what happens when full — silent drop risk under high RPC traffic.

6. **Dynamic casts in render bundle**: `RendererBundle::reset()` uses `dynamic_cast` twice to detect optional interfaces. Fragile if subclass hierarchy changes.

### Medium Issues

7. **Null checks via assertions**: `GridHostBase::callbacks()` asserts pointer is non-null but doesn't guard against misuse. Should be null checks with clear error messages.

8. **Thread safety in RPC**: `running_` is atomic while `responses_` and `notifications_` are behind mutexes. Benign in practice but not formally provable.

9. **Disabled copy but no explicit move constructors** in some classes (`FontManager`, `TextShaper`). Should explicitly `=delete` copy constructors for clarity.

10. **High static cast count** (459 occurrences): Indicates possible type system inconsistency mixing `int`/`uint`/`float` for dimensions.

---

## 3. Testing Gaps

### What's Well Tested (87 test files)
- VT parser, terminal state machine
- Grid rendering pipeline, dirty cells, highlight attributes
- Unicode handling, wide characters, ligatures
- RPC communication, Neovim process startup/crashes
- UI event handling, keybinding dispatch, fuzzy matching
- Config parsing and I/O roundtrips
- DPI hotplug, window resizing
- Render test infrastructure with reference images

### Critical Gaps

1. **No HostManager split/close stress tests**: Rapid split/close cycles, zoom during pending splits, rename during shutdown — all untested.

2. **Host lifecycle edge cases**: `pump()` before `initialize()`, double `shutdown()`, host crash between `pump()` calls — no coverage.

3. **Cross-host communication**: `dispatch_to_nvim_host()` to non-existent host, dispatch during shutdown, malformed action strings.

4. **Renderer integration limits**: Atlas exhaustion (>2048x2048 glyphs), concurrent handle creation/destruction, frame-dropping under high cell-update load.

5. **Memory sanitizer gaps**: ASan preset exists; no evidence of TSan, UBSan, or MSan in CI. WI 112 (tsan-build-preset) on backlog.

6. **Config persistence edge cases**: Concurrent reloads, malformed TOML mid-reload, partial reload success leaving inconsistent state.

---

## 4. Maintainability

### Positive Signals
- Clear dependency boundaries in each lib's `CMakeLists.txt`
- Consistent naming conventions throughout (snake_case functions, kCamelCase constants, ICapitalCase interfaces)
- Config-driven behavior for keybindings, font size, scroll speed
- Diagnostic overlay for real-time debugging
- Extensive structured logging with category filtering

### Friction Points

1. **InputDispatcher dependency injection is bloated**: `Deps` struct has 20+ function pointers (`tab_bar_height_phys`, `hit_test_tab`, `hit_test_pane_pill`, etc.). One missing callback and mouse input silently fails.

2. **Test fixtures are scattered**: No centralized `FakeWindow`, `FakeRenderer`, `FakeHost` library. Each test reinvents mocks. Adding a new host type requires understanding many mock patterns.

3. **CMake is moderately complex**: `CompileShaders.cmake` and `CompileShaders_Metal.cmake` duplicate logic. Shader staging is verbose.

4. **Platform callbacks leak type safety**: SDL callbacks are C-style function pointers; if SDL3 changes event layout, the lambda signatures in `sdl_window.cpp` must be updated without compiler help.

---

## 5. Platform Code Hygiene

### Excellent Separation
- **Zero** platform-specific `#ifdef`s in shared libraries
- Only 36 ifdefs total in entire codebase, mostly in `app/macos_menu.mm` and window backend selection
- Platform selection handled at build time via CMake (`if(APPLE)` → Metal shaders, else Vulkan)

### Minor Issues
- macOS app bundle resource staging is verbose (metal shader copying, font paths) — could be a helper function
- Megacity assets: `if(DRAXUL_ENABLE_MEGACITY AND EXISTS "${CMAKE_SOURCE_DIR}/assets/megacity")` silently skips if assets missing; should error when `DRAXUL_ENABLE_MEGACITY=ON`

---

## 6. Build System

### Strengths
- CMake 3.25+ with FetchContent for all dependencies
- Automatic ccache/sccache detection
- Sanitizer integration (address, leak, undefined)
- Code coverage support (`-fprofile-instr-generate`)

### Issues

1. **Render test discovery is manual**: `basic-view`, `cmdline-view`, etc. hardcoded in `CMakeLists.txt`; adding a new render test requires editing the build file.

2. **Font staging has no error guard**: If font `.ttf` files don't exist, silently skipped. Should error or provide a fallback.

3. **No package targets**: No `.dmg`, `.msi`, or `AppImage` packaging configured.

4. **Shader dependency tracking is implicit**: Editing a `.metal`/`.glsl` may not always retrigger recompile.

---

## 7. Thread Safety

### Correct Patterns
- RPC reader thread isolated; communicates via atomic + mutex + condition_variable
- `MainThreadChecker` (debug-only) ensures Grid/Host/Window aren't accidentally accessed from background threads
- Notification and response queues protected by separate mutexes

### Potential Races

1. **Grid dirty marks under concurrent access**: Grid is accessed from RPC thread (set_cell, scroll) and main thread (get_dirty_cells, clear_dirty). No explicit mutex on Grid; safety relies on `MainThreadChecker` discipline which is debug-only.

2. **Callback lifetime in hosts**: `GridHostBase` stores `IHostCallbacks*` as borrowed pointer. If App destroys callback while host runs, `callbacks()` dereference crashes. WI 64 suggests this was a real bug.

3. **Font service under DPI changes**: `on_display_scale_changed()` reinitializes `TextService`; if a host is mid-render and dereferences `text_service_`, partially-initialized state could be seen.

4. **wait_for race**: Between checking `impl_->running_` and calling `wait_for`, `running_` could become false. Benign in practice but not formally safe.

---

## 8. Error Handling

### Where Errors Are Silently Swallowed

1. **Clipboard operations**: `copy_to_clipboard()`, `paste_from_clipboard()` log WARN but don't notify user.

2. **Font fallback failures**: If fallback font can't load, warning logged but grid continues. User sees missing glyphs without knowing why.

3. **RPC request timeouts**: kRpcRequestTimeout = 5 seconds; logged as WARN but no toast notification.

4. **Config reloads**: `reload_config()` failure logs but doesn't surface to user.

5. **Atlas overflow**: Glyphs disappear, logged as ERROR but no user notification and no fallback.

### Good Error Recovery
- Startup failures display toasts with reason
- Host crashes trigger error handling
- Config load warnings are displayed as toasts
- Render test failures collect error message in `last_render_test_error_`

---

## Module Quality Summary

| Module | Test Coverage | Concerns | Grade |
|--------|---------------|----------|-------|
| draxul-types | Low (types-only, no logic) | POD only | A |
| draxul-window | Medium | SDL3 glue, DPI handling | B+ |
| draxul-renderer | Medium | GPU backends complex; parity untested | B |
| draxul-font | Low | Fallback chain linear; atlas overflow silent | B |
| draxul-grid | High | VT parser well-tested | A |
| draxul-host | High | Terminal emulation; good coverage | A- |
| draxul-nvim | High | Neovim RPC, UI events | A |
| draxul-ui | Low | ImGui glue; no unit tests | B |
| draxul-config | High | TOML parsing, roundtrip tested | A |
| app | Medium | Complex initialization path | B |

---

# Top 10 GOOD Things

1. **Immaculate platform code separation** — Zero platform-specific `#ifdef`s in shared libraries; Vulkan/Metal backends completely isolated. Build-time selection via CMake.

2. **Interface-first architecture** — Every component exports a pure virtual base class; enables testing, dependency injection, and loose coupling.

3. **Dependency injection everywhere** — `AppDeps`, `HostManager::Deps`, `InputDispatcher::Deps` use struct-based injection; zero singletons; trivial to swap implementations.

4. **Exceptional test coverage** — 87 test files, 27K+ LOC of tests covering VT parser, terminal emulation, grid rendering, RPC, keybindings, DPI handling, config roundtrips.

5. **Clean build system** — CMake 3.25+, FetchContent for dependencies, automatic ccache/sccache, sanitizer integration, code coverage support.

6. **Transparent diagnostics overlay** — F12 toggle shows real-time frame timing, dirty cell count, glyph atlas stats, startup phases.

7. **Meticulous logging infrastructure** — `DRAXUL_LOG_*` with category filtering, dual output (stderr + file), configurable levels via CLI flags.

8. **Acyclic dependency graph** — types < window < renderer < font < grid < host < app; zero circular dependencies.

9. **Excellent naming consistency** — Consistent throughout (snake_case functions, kPrefixConstants, IInterfaceNames) despite rapid development pace.

10. **Comprehensive feature parity** — Terminal emulation matches xterm (VT100+, SGR colors, DECSTBM, mouse modes, bracketed paste, OSC 7/52); Neovim integration is deep; MegaCity visualization is sophisticated.

---

# Top 10 BAD Things

1. **Notification queue backpressure is undefined** — Queue can reach 4096 depth before warning; unclear what happens when full (silent drop? block?). Risk: lost RPC notifications under high load.

2. **Host callback lifetime is fragile** — `IHostCallbacks*` stored as raw borrowed pointer; if App destroys callback before host shutdown, dangling pointer crash. WI 64 confirms this was a real bug.

3. **Inconsistent error handling patterns** — No unified `Result<T, Error>` type; `bool + error()`, `std::optional`, and silent-fail patterns all coexist.

4. **RPC request timeout has no user feedback** — If Neovim hangs > 5 seconds, logged as WARN only. Users won't know why GUI is unresponsive.

5. **Atlas exhaustion has no fallback** — If glyph cache exhausts 2048×2048, glyphs disappear silently. No dynamic reallocation, no user notification.

6. **No thread safety formalism** — Grid and other structures rely on `MainThreadChecker` (debug-only asserts); in release mode, concurrent access from RPC thread is unchecked.

7. **InputDispatcher dependency injection is bloated** — `Deps` struct has 20+ function pointers; one missing callback and mouse input silently fails with no diagnostic.

8. **Test fixtures are scattered** — No centralized `FakeWindow`/`FakeRenderer`/`FakeHost` library; each test reinvents mocks independently.

9. **Render test discovery is manual** — render scenarios hardcoded in `CMakeLists.txt`; adding a new test requires editing the build file.

10. **Long initialization path** — `App::initialize()` is 200+ lines; startup failures could originate from any of 10+ subsystems without clear root-cause UI.

---

# Best 10 Features to Add (Quality of Life)

1. **Unified `Result<T, Error>` type** — Replace the `bool + error()` / `std::optional` / silent-fail mix with a type-safe result enum that makes error cases impossible to ignore at call sites.

2. **User feedback for RPC timeouts** — Show a dismissible toast ("Neovim is not responding") when an RPC request exceeds the timeout threshold. Add exponential backoff instead of always waiting 5 seconds.

3. **Dynamic glyph atlas growth** — When the 2048×2048 atlas fills up, allocate a second atlas page rather than silently dropping glyphs. Toast the user if an emergency eviction occurs.

4. **Async config reload with validation** — Validate the new `config.toml` in a background thread before hot-swapping; if invalid, show a toast with the offending line number and keep the current config.

5. **Host telemetry in diagnostics** — Extend the F12 overlay with per-host RPC latency, grid dirty-cell rates, and memory usage; add a toast warning if RPC latency spikes above a threshold.

6. **Split layout snapshot/restore** — Save the full split-tree layout to `~/.local/share/draxul/layout.json` (or equivalent) and restore it on next launch, including pane sizes and working directories.

7. **Keybinding conflict detector at startup** — Warn via toast if two configured keybindings map to the same chord; suggest which one wins and how to resolve.

8. **Config migration framework** — Version the config schema (`config_version = 1`); auto-migrate old formats silently and log the migration steps. Prevents silent breakage when fields are renamed.

9. **Render profiler with per-pass timing** — Add a stacked bar chart in the diagnostics overlay showing atlas-upload, grid-render, and UI-render time per frame.

10. **Paste safety: multi-line confirmation threshold configurable** — Allow `paste_confirm_lines` to accept a range (e.g., warn only when paste > N lines) and let the user confirm once per session rather than per-paste.

---

# Best 10 Tests to Add (Stability)

1. **HostManager split/close stress test** — 1000 rapid split/close cycles; assert tree structure is valid and all pane descriptors are consistent after each step.

2. **RPC notification queue backpressure** — Enqueue >4096 RPC notifications in rapid burst; verify the queue drains correctly and no notifications are silently lost.

3. **Host lifecycle state machine** — `initialize()` → `pump()` → `shutdown()` → `pump()` must not crash; `shutdown()` twice must be idempotent; `pump()` before `initialize()` must return error, not crash.

4. **Font atlas exhaustion** — Fill the glyph cache with >10K unique glyphs; verify no glyphs disappear and the renderer degrades gracefully or extends the atlas.

5. **Config reload under host activity** — Trigger `reload_config()` while synthetic key events are being processed; verify no lost key events and no race conditions.

6. **DPI hotplug during active frame** — Call `on_display_scale_changed()` while a render frame is in-flight; verify no GPU resource leaks or use-after-free.

7. **Concurrent grid dirty marks** — Simulate high-frequency grid updates from a background thread while the main thread reads dirty cells; run under TSan to detect races.

8. **InputDispatcher with null dependency callbacks** — Construct `InputDispatcher` with `Deps` fields set to `nullptr`; verify graceful no-op (not segfault) for every input event type.

9. **Renderer shutdown with pending frames** — Call `shutdown()` before `end_frame()` completes; verify all GPU fences are waited and all resources are freed (run under ASan).

10. **`dispatch_to_nvim_host()` with no live Neovim** — Call with no Neovim host registered; verify a meaningful error is returned and the app does not crash or deadlock.

---

# Worst 10 Existing Features (Fragile / Poorly Implemented)

1. **Silent RPC notification drop** — No monitoring, no backpressure signal to Neovim, queue just grows. Could silently drop grid-line events under heavy load.

2. **Raw pointer callback lifetime** — Borrowed `IHostCallbacks*` everywhere with no formal lifetime contract; one misplaced owner transfer and GUI crashes with no diagnostic.

3. **Atlas overflow handling** — Glyphs disappear without warning; no fallback rasterization strategy; no user notification.

4. **RPC timeout handling** — Always 5 seconds, no backoff, no user feedback. GUI appears hung with no explanation.

5. **Font fallback chain is linear and opaque** — If primary font is broken, fallbacks tried sequentially with no progress indicator; user sees blank cells with no toast.

6. **Terminal color parsing is strict** — Invalid hex colors in config result in blank terminal instead of falling back to defaults; error is easy to miss.

7. **Scrollback buffer is fixed-size and hardcoded** — 2000 lines; users with high-scroll workflows hit limit silently with no configurable override.

8. **Paste confirmation is all-or-nothing** — If `paste_confirm_lines = 1`, every paste needs confirmation regardless of context; no per-action granularity or session-level "trust" flag.

9. **Config reload is not transactional** — Reloads can partially succeed (e.g., font updates but keybindings fail), leaving inconsistent state with no rollback path.

10. **InputDispatcher `Deps` bloat** — 20+ function pointers with no validation that all are set; missing one silently breaks mouse/keyboard input in that subsystem with no log entry pointing to the missing callback.

---

*Overall assessment: **7.5/10**. Strong execution, solid architecture, excellent tests. Main weaknesses are error handling consistency, silent failure modes, and lack of user feedback for transient failures.*
