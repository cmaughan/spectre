Now I have enough information to produce the full report. Here it is:

---

# Draxul Codebase Review Report

*Generated: 2026-03-20 | Reviewer: Claude Sonnet 4.6*

---

## Executive Summary

Draxul is a cross-platform Neovim GUI frontend written in C++20. It spawns `nvim --embed`, communicates via msgpack-RPC, and renders the terminal grid using Vulkan (Windows) or Metal (macOS). The codebase is mature, professionally maintained, and shows clear evidence of systematic refactoring and AI-agent-friendly design. This report covers module layout, code quality, testing coverage, feature opportunities, and worst technical debt.

---

## Module Layout

```
draxul-types         (header-only POD types, logging, unicode)
├── draxul-window    (IWindow + SDL3)
├── draxul-renderer  (IGridRenderer + Vulkan/Metal backends)
├── draxul-font      (FreeType + HarfBuzz glyph pipeline)
├── draxul-grid      (2D cell array + dirty tracking)
├── draxul-nvim      (NvimProcess, msgpack RPC, UiEventHandler)
├── draxul-host      (TerminalHostBase, VtParser, PTY backends)
├── draxul-ui        (ImGui diagnostics panel)
├── draxul-megacity  (optional 3D demo)
├── draxul-app-support (AppConfig, GridRenderingPipeline, CursorBlinker, render tests)
└── app/             (App orchestrator, GuiActionHandler, InputDispatcher, HostManager)
```

Dependency flow is strictly downward. The app layer is intentionally thin — all logic lives in libraries.

---

## Source File Inventory

| Module | Files | Notable |
|---|---|---|
| `app/` | 9 | App, GuiActionHandler, InputDispatcher, HostManager, main |
| `libs/draxul-types/` | 9 | Color, CellUpdate, AtlasRegion, CellText, events, logging |
| `libs/draxul-grid/` | 3 | Grid, IGridSink, HighlightTable |
| `libs/draxul-font/` | 8 | TextService, GlyphCache, FontEngine, LigatureAnalyser |
| `libs/draxul-renderer/` | 11 | IRenderer, Vulkan backend, Metal backend, RendererState |
| `libs/draxul-nvim/` | 7 | NvimProcess, NvimRpc, MpackCodec, UiEventHandler, Input |
| `libs/draxul-host/` | 18 | TerminalHostBase, VtParser, VtState, AltScreenManager, MouseReporter, SelectionManager, ScrollbackBuffer, PTY hosts |
| `libs/draxul-app-support/` | 10 | AppConfig, GridRenderingPipeline, CursorBlinker, RenderTest, UiRequestWorker |
| `libs/draxul-ui/` | 2 | UiPanel |
| `libs/draxul-megacity/` | 4 | MegacityHost, optional 3D demo |
| `shaders/` | 10 | Metal + Vulkan (GLSL) shaders for BG/FG grid passes + 3D demo |
| `tests/` | 53 | Unit, integration, fuzz, regression |

---

## Top 10 Good Things

### 1. Clean Abstract Interfaces
`IWindow`, `IGridRenderer`, `IHost`, `IGlyphAtlas`, `IRpcChannel` — all pure virtual interfaces. The app and test code never touch platform-specific types. Swapping Vulkan for Metal or SDL for another backend touches zero app-layer code.

### 2. Dependency-Injection Seams in AppOptions
`AppOptions` carries `window_init_fn`, `renderer_create_fn`, and `config_overrides`. Tests inject `FakeRenderer` and `FakeWindow` without forking any production path. This pattern is used consistently across all 53 test files.

### 3. Structured InitRollback Pattern
`App::initialize()` uses a scoped `InitRollback` struct (armed on entry, disarmed on success) that calls `shutdown()` if any step fails. Partial initialization is never leaked. Integration tests (`startup_rollback_tests.cpp`) cover this path directly.

### 4. Unidirectional Data Flow
Events flow: SDL → InputDispatcher → IHost → Grid → GridRenderingPipeline → IGridRenderer. No callbacks up the dependency graph. This makes reasoning about state mutation straightforward and makes the codebase easy to hand to parallel agents.

### 5. Thorough Test Suite
53 test files covering: grid operations, VT parser (including fuzz), msgpack codec (including fuzz), RPC backpressure, DPI scaling, font fallback corpus, shell crash recovery, scrollback overflow, cursor blinker, UI panel layout, keybinding dispatch, alt-screen resize, clipboard round-trip, and more. Catch2 v3 with scoped log capture for deterministic output.

### 6. VT Parser as a Standalone State Machine
`VtParser` takes callbacks at construction and has no knowledge of the grid, host, or process. This makes it independently fuzzable and testable. `vt_parser_fuzz_tests.cpp` and `terminal_vt_tests.cpp` exercise it in isolation.

### 7. Two-Level Dirty Tracking in Grid
`Grid` maintains both a `dirty_marks_` bitfield (O(1) is-dirty query) and a `dirty_cells_` vector (O(dirty) iteration). The rendering pipeline only processes cells that changed. This keeps frame times proportional to actual change, not grid size.

### 8. AddressSanitizer / LeakSanitizer Integration
The `mac-asan` CMake preset enables both ASan and LSan across all Draxul targets (not dependencies). CI runs tests under this preset. The `tests/` tree's Catch2 entrypoint is wired to run cleanly under sanitizers.

### 9. Work-Item Tracking Discipline
Plans are tracked in `plans/work-items/`, `plans/work-items-complete/`, and `plans/work-items-icebox/` with consistent naming (`NN description -tag.md`). Every item records motivation, implementation plan, test plan, and sub-agent split. This is the most agent-friendly project-management structure in the reviewed codebases.

### 10. Startup Timing Instrumentation
`App::initialize()` measures each init step with `time_step()` and accumulates `startup_steps_` / `startup_total_ms_`. These are surfaced in the ImGui diagnostics panel at runtime. Makes startup regressions immediately visible without profiling tools.

---

## Top 10 Bad Things

### 1. `CellText::kMaxLen = 32` Silent Truncation
`CellText::assign()` silently truncates grapheme clusters longer than 32 bytes. The TODO comment (`// TODO: consider std::string for >32-byte clusters`) has existed since the type was introduced. Emoji with multiple modifiers (skin tone + ZWJ sequences) can exceed this. The truncation produces a malformed UTF-8 string in the cell with no error signal.

```cpp
// libs/draxul-grid/include/draxul/grid.h:31
len = static_cast<uint8_t>(std::min(sv.size(), static_cast<size_t>(kMaxLen)));
```

### 2. Hardcoded ANSI Palette
The 16-colour ANSI palette is hardcoded as float literals in `terminal_host_base.cpp:22-43`. There is no way for the user to customise ANSI colours in `config.toml`. This is noted as icebox item `#33 configurable-ansi-palette` but the hardcoded values also silently ignore anything the connected terminal application sends via OSC 4 (palette assignment), which is a common terminal feature.

### 3. Unbounded RPC Notification Queue
`NvimRpc` uses an unbounded `std::vector<RpcNotification>` queue between the reader thread and the main thread. Under a burst of Neovim redraw events (large file open, bulk replace) the queue can grow without bound. Documented in `plans/design/belt-and-braces.md` as a known risk, but no backpressure mechanism or queue depth limit is implemented.

### 4. `on_display_scale_changed` Duplicates `TextServiceConfig` Construction
`App::on_display_scale_changed()` and `App::initialize_text_service()` both manually construct `TextServiceConfig` from `config_.*` fields. If a new `TextServiceConfig` field is added, both sites must be updated. A helper such as `make_text_service_config()` would eliminate this.

```cpp
// app/app.cpp:429-432 and 191-193 — two identical blocks
TextServiceConfig text_config;
text_config.font_path = config_.font_path;
text_config.fallback_paths = config_.fallback_paths;
text_config.enable_ligatures = config_.enable_ligatures;
```

### 5. `set_main_thread_id` Global Free Function
`nvim_rpc.h:211` exposes a global `set_main_thread_id(std::thread::id)` function with no guard against being called multiple times or from the wrong thread. `NvimRpc::request()` uses this global to assert it is not called from the main thread. A per-instance stored thread ID would be safer and testable.

### 6. `MpackValue::type()` Is O(n) via Sequential `holds_alternative`
`MpackValue::type()` chains 9 `std::holds_alternative<T>` calls. While the variant's `index()` method would give the same info in O(1), the code deliberately avoids it to decouple enum values from variant declaration order. The comment is correct, but the cost is 9 template instantiations on every type query in the hot RPC decode path. A static lookup table keyed on `storage.index()` with compile-time enforcement would be both fast and correct.

### 7. `megacity` Module Is Effectively Dead Code
`draxul-megacity` provides a 3D spinning-cube demo host that uses `dynamic_cast<I3DPassProvider*>()` to inject an extra render pass. It is linked into the production binary, adds complexity to the renderer abstraction (`I3DPassProvider`, `vk_cube_pass`, `megacity_render_vk`, `megacity_render.mm`), and has no users. Icebox item `#17 megacity-removal-refactor` exists but is unscheduled.

### 8. `pending_window_activation_` Flag With Subtle Platform Behaviour
`App::pump_once()` calls `window_.activate()` on the first iteration if `pending_window_activation_` is set. On CI, `options_.activate_window_on_startup` is false to avoid stealing focus. The flag and its conditioning are easy to misread. A prior bug (`38 window-activation-unconditional-ci`) was fixed here, suggesting this area is fragile.

### 9. `AppConfig` SDL3 Coupling in `app-support`
`libs/draxul-app-support/src/app_config.cpp:25` references `SDL_Keymod` and `SDL_KMOD_*` constants to parse keybinding modifiers. This couples the config library to SDL3's type system. Icebox item `#03 appconfig-sdl3-coupling-bug` calls this out but it is unresolved. A platform-neutral modifier bitmask in `draxul-types` would remove the coupling.

### 10. No Mid-Frame Resize Guard
If the window is resized between `renderer_.grid()->begin_frame()` and `renderer_.grid()->end_frame()` in `App::pump_once()`, the GPU cell buffer dimensions may not match the swapchain image. There is no explicit guard or assertion at this boundary. Icebox item `#09 grid-mid-frame-resize-test` acknowledges this gap but no fix is present.

---

## Top 10 Features That Would Improve Quality of Life

### 1. IME Composition Display (Icebox #29)
SDL3 provides `SDL_EVENT_TEXT_EDITING` with preedit text and cursor position. Draxul receives these events but does not render the in-progress composition inline. Users typing CJK, Korean, or emoji via input method editors see nothing until they confirm. Implementing an inline preedit overlay (even just an underlined region) would unblock a large class of international users.

### 2. Live Config Reload (Icebox #56)
`config.toml` is loaded at startup only. Changing font size, ligature settings, or keybindings requires a full restart. Watching the config file with `inotify`/`FSEvents`/`ReadDirectoryChangesW` and re-applying changed fields in-place would dramatically improve the configuration experience, especially since `AppConfig::save()` already round-trips correctly.

### 3. Configurable ANSI Palette (Icebox #33)
The 16-colour ANSI palette and 256-colour xterm cube are both hardcoded. A `[palette]` section in `config.toml` accepting hex colour values for indices 0–15 would allow users to match their terminal theme, and would align Draxul's terminal-emulator mode with every other modern terminal emulator.

### 4. URL Detection and Click-to-Open (Icebox #20)
Terminal output frequently contains file paths and URLs. Detecting common URL patterns (https://, file://, relative paths) and making them clickable (open in browser / open file / copy to clipboard on click) is a high-value ergonomic feature. The `SelectionManager` already tracks cell ranges; extending it to detect and highlight URL spans is a natural fit.

### 5. Native Tab Bar (Icebox #57)
Running multiple Neovim sessions or terminal panes requires separate OS windows. A native tab bar (SDL3 has no built-in tab widget, but ImGui does) would let users switch between sessions in a single window. The `HostManager` + `IHost` abstraction is already designed to support multiple hosts.

### 6. Remote Neovim Attach (Icebox #30)
The current architecture spawns `nvim --embed` locally. Supporting `nvim --listen` / `--server` attach (connecting over TCP or a Unix socket rather than pipes) would enable remote editing, pairing workflows, and reconnect-on-disconnect without changing any rendering or input code.

### 7. Command Palette (Icebox #60)
A fuzzy-searchable command palette (`Ctrl+Shift+P` style) exposing all `GuiAction` entries (font size, diagnostics toggle, copy/paste, etc.) would be more discoverable than reading documentation. The existing `GuiActionHandler` dispatch-map is already a clean list of actions to enumerate.

### 8. Window State Persistence (Icebox #36)
Window position is not saved. Draxul saves window width and height to `config.toml` on shutdown, but not `x`/`y`. On multi-monitor setups the window always opens on the primary display. Adding position persistence is a one-liner in `App::shutdown()` and `AppConfig`.

### 9. Configurable Scrollback Capacity (Icebox #34)
`ScrollbackBuffer` has a fixed capacity. Heavy terminal users (long build logs, streaming output) hit the limit and lose history silently. A `scrollback_lines = N` key in `config.toml` with a documented default and maximum would let power users tune it.

### 10. Font Fallback Inspector (Icebox #61)
When a glyph is missing from the primary font and falls back (or falls through to a replacement glyph), there is no user-visible indication of which fallback font was used. A diagnostics panel section showing "last 10 glyph fallbacks" (codepoint → resolved face name) would help users debug font configuration issues without reading logs.

---

## Top 10 Tests That Would Improve Stability

### 1. Grid Mid-Frame Resize (Icebox #09)
Simulate a resize event arriving between `begin_frame()` and `end_frame()` via `FakeRenderer` + `FakeWindow`. Assert that no out-of-bounds GPU buffer write occurs and that the renderer recovers cleanly on the next frame. This covers the acknowledged gap in the current test suite.

### 2. Atlas Post-Reset Pixel Correctness (Icebox #10)
After the glyph atlas resets (triggered by overflow), force a full re-upload and capture a render snapshot. Compare pixel-by-pixel to a pre-reset capture for the same text. This would catch any glyph-region remapping bugs that the current `ligature_atlas_reset_tests.cpp` doesn't cover at the pixel level.

### 3. SDL Key Encoding Exhaustive (Icebox #11)
`encode_key_tests.cpp` covers common keys but not the full SDL3 keycode space. A data-driven exhaustive test that feeds every `SDL_Keycode` through `NvimInput::encode_key` and asserts the output matches the Neovim key protocol specification would prevent regressions when SDL3 updates its keycode enum.

### 4. Cursor Blinker DPI Change (Icebox #07)
`cursor_blinker_tests.cpp` tests blink timing but not what happens when DPI changes mid-session. A test that changes `display_ppi_` and asserts the cursor period is recalculated correctly (blink rate is typically a fraction of cell height) would prevent the cursor freezing or blinking at the wrong rate after a monitor change.

### 5. Input Dispatcher Modal State (Icebox #09)
`input_dispatcher_routing_tests.cpp` covers normal key routing but not the case where the UI panel is visible and the input should be swallowed / redirected. A test asserting that keyboard input is withheld from the host while the diagnostics panel is focused, and restored when it is hidden, would prevent modal confusion regressions.

### 6. Alt-Screen Resize Restore (Icebox #11)
Resize the terminal while the alternate screen is active, then restore the primary screen. Assert that the primary screen grid dimensions match the post-resize size, not the pre-resize snapshot stored in `AltScreenManager`. The related bug `#06 altscreen-resize-mismatch` was fixed but has no regression test.

### 7. Startup Rollback + Clipboard State (Icebox #22)
`startup_rollback_tests.cpp` exercises rollback when renderer init fails. A complementary test injecting a failure after the clipboard subsystem is initialised (e.g., after `SDL_Init`) would confirm the SDL teardown path is clean and does not leak SDL state that affects subsequent test cases.

### 8. VT Parser Fuzz Corpus Expansion (Icebox #02)
`vt_parser_fuzz_tests.cpp` exists but the corpus is small. Importing libFuzzer corpus entries from other terminal emulator fuzz projects (xterm, foot, wezterm) would surface edge cases in multi-byte OSC bodies, sub-parameter CSI sequences (`38:2:r:g:b`), and malformed UTF-8 mid-sequence.

### 9. RPC Codec Fuzzing (Icebox #21)
`mpack_fuzz_tests.cpp` fuzzes the msgpack decoder but only at the byte level. A higher-level fuzzer that generates structurally valid msgpack arrays with random RPC method names and parameter shapes would test `UiEventHandler`'s dispatch table and parameter extraction without needing a real Neovim process.

### 10. Grid Scroll Stress Under Concurrent Dirty Marks (Icebox #05)
`grid_tests.cpp` tests `Grid::scroll()` but not under concurrent dirty-mark pressure. A stress test that interleaves `scroll()` calls with `set_cell()` calls using randomised parameters and asserts dirty-cell consistency at every step would guard against the class of wide-char boundary bugs that `#11 grid-scroll-wide-char-boundary` fixed.

---

## Worst 10 Features

### 1. The Megacity 3D Demo Module
`draxul-megacity` is a spinning-cube demo with no user value. It adds ~600 lines of Vulkan and Metal render code, a `dynamic_cast` in the renderer hot path, two extra shader files, and compile-time coupling between the renderer backend and an optional guest feature. It is the textbook definition of dead weight in a production binary. Icebox item `#17 megacity-removal-refactor` exists but is unscheduled.

### 2. Hardcoded ANSI Palette With No OSC 4 Support
The ANSI colour palette is baked in as magic floats with no escape hatch. OSC 4 (colour assignment), OSC 10/11 (default fg/bg query), and OSC 12 (cursor colour) are all silently ignored. Any TUI application that dynamically recolours the palette (some vim themes do this) will render incorrectly.

### 3. No IME Composition Rendering
SDL3 fires `SDL_EVENT_TEXT_EDITING` with preedit text on every keystroke in an active input method. Draxul discards these events. Users typing CJK or using dead-key composition see no feedback until confirmation. This is a showstopper for a significant portion of the world's keyboard users.

### 4. Silent Cluster Truncation in `CellText`
`CellText::kMaxLen = 32` truncates any grapheme cluster longer than 32 bytes without logging, asserting, or returning an error. Modern emoji (flag sequences, multi-person skin-tone ZWJ sequences) routinely exceed 32 bytes. The truncated bytes produce a malformed UTF-8 suffix that HarfBuzz will either skip or misshape.

### 5. No Background Transparency (Icebox #34)
Translucent terminal windows are a mainstream feature in macOS, Windows, and Linux desktop environments. Draxul always renders fully opaque. Adding a `background_opacity` config key and updating the renderer clear-color alpha would be a modest change with high user demand.

### 6. Scrollback Limited to Fixed Capacity With No Config Key
The scrollback buffer capacity is hardcoded. Once it fills, the oldest lines are evicted silently. There is no `scrollback_lines` key in `config.toml`, no log warning at overflow, and no way for users to tune the tradeoff between memory use and history depth.

### 7. No Full `guicursor` Support (Icebox #19)
Neovim's `guicursor` option controls cursor shape, blink timing, and blink start/stop delays for each mode (normal, insert, replace, visual, etc.). Draxul partially honours cursor shape changes but does not implement per-mode blink timing from `guicursor`. Plugins that rely on mode-specific cursor behaviour (e.g., mode indicators in status lines) are partially blind.

### 8. No Remote Neovim Attach
Every session requires a locally spawned `nvim --embed`. There is no support for connecting to an already-running Neovim (`nvim --server`/`--listen`) over a Unix socket or TCP. Remote development workflows (SSH + local GUI) are completely unsupported.

### 9. No Hierarchical Config (Icebox #37)
`config.toml` is a flat file with no inheritance, no project-local config, and no env-based overrides (beyond `APPDATA`/`HOME`). Users cannot maintain per-project font sizes or keybinding overrides. Adding XDG_CONFIG_DIRS-style layering or a `.draxul.toml` project override would align Draxul with modern tool expectations.

### 10. `AppConfig` Coupled to SDL3 Modifier Types
`libs/draxul-app-support/src/app_config.cpp` uses `SDL_Keymod` and `SDL_KMOD_*` to represent modifier bitmasks in the parsed keybinding table. This pulls SDL3 into what should be a platform-neutral config parsing library. Any test or tool that links `draxul-app-support` without SDL3 must work around this coupling. Icebox item `#03 appconfig-sdl3-coupling-bug` is open but unscheduled.

---

## Summary Tables

### Top 10 Good

| # | Area | Verdict |
|---|---|---|
| 1 | Abstract interfaces (IWindow, IRenderer, IHost) | Excellent platform isolation |
| 2 | DI seams in AppOptions | Enables realistic testing without mocks |
| 3 | InitRollback pattern | Safe partial-init teardown |
| 4 | Unidirectional data flow | Easy to reason about, agent-friendly |
| 5 | 53-file test suite | High coverage across all layers |
| 6 | VtParser as standalone state machine | Independently fuzzable |
| 7 | Two-level dirty tracking in Grid | Frame time proportional to change |
| 8 | ASan/LSan CI integration | Memory safety enforced continuously |
| 9 | Work-item tracking discipline | Best-in-class for multi-agent development |
| 10 | Startup timing instrumentation | Regressions visible at runtime |

### Top 10 Bad

| # | Area | Issue |
|---|---|---|
| 1 | `CellText::kMaxLen = 32` | Silent cluster truncation, data loss |
| 2 | Hardcoded ANSI palette | No user customisation, no OSC 4 |
| 3 | Unbounded RPC queue | Memory risk under burst load |
| 4 | `TextServiceConfig` duplicated | Two-site maintenance hazard |
| 5 | Global `set_main_thread_id` | Threading footgun, untestable |
| 6 | `MpackValue::type()` O(n) | Hot-path inefficiency |
| 7 | Megacity dead code | Maintenance burden, runtime coupling |
| 8 | `pending_window_activation_` | Historically fragile, platform-conditional |
| 9 | SDL3 in `app-support` config | Wrong layer coupling |
| 10 | No mid-frame resize guard | Latent GPU corruption path |

### Best 10 Features to Add

| # | Feature | Impact |
|---|---|---|
| 1 | IME composition display | Unblocks international users |
| 2 | Live config reload | Major QoL, no restart needed |
| 3 | Configurable ANSI palette | TUI theme compatibility |
| 4 | URL detection + click | High-frequency daily use |
| 5 | Native tab bar | Multi-session UX |
| 6 | Remote Neovim attach | Remote development workflows |
| 7 | Command palette | Feature discoverability |
| 8 | Window position persistence | Multi-monitor UX |
| 9 | Configurable scrollback | Power user tuning |
| 10 | Font fallback inspector | Font debugging UX |

### Best 10 Tests to Add

| # | Test | Stability Impact |
|---|---|---|
| 1 | Grid mid-frame resize | Prevent latent GPU corruption |
| 2 | Atlas post-reset pixel correctness | Catch remapping bugs |
| 3 | SDL key encoding exhaustive | Prevent keycode regressions |
| 4 | Cursor blinker DPI change | Prevent cursor freeze after monitor change |
| 5 | Input dispatcher modal state | Prevent key-swallowing regressions |
| 6 | Alt-screen resize restore | Regression-test fixed bug #06 |
| 7 | Startup rollback + clipboard | Complete SDL teardown coverage |
| 8 | VT parser fuzz corpus expansion | Catch real-world edge cases |
| 9 | RPC codec structural fuzzing | Test dispatch without live Neovim |
| 10 | Grid scroll stress + dirty marks | Guard wide-char boundary invariants |

### Worst 10 Features

| # | Feature | Why It's Bad |
|---|---|---|
| 1 | Megacity 3D demo | Dead code, production coupling |
| 2 | Hardcoded ANSI palette | Breaks dynamic TUI themes |
| 3 | No IME composition | Unusable for CJK users |
| 4 | Silent cluster truncation | Data corruption without signal |
| 5 | No background transparency | Missing mainstream feature |
| 6 | Fixed scrollback capacity | Silent history loss |
| 7 | Partial guicursor support | Mode-specific cursor broken |
| 8 | No remote Neovim attach | Remote dev completely unsupported |
| 9 | No hierarchical config | No per-project overrides |
| 10 | SDL3 in config library | Wrong-layer coupling, untestable |
