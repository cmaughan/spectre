Now I have sufficient information to compile the full report. Here it is:

---

# Draxul Codebase Review

## Overview

Draxul is a cross-platform GPU-accelerated Neovim GUI frontend written in C++20. It supports Windows (Vulkan) and macOS (Metal) via SDL3 windowing, with a strict layered library architecture, comprehensive test coverage, and multi-agent code review tooling.

---

## Module Map

```
draxul-types (header-only POD)
    ↓ used by all libraries
draxul-window   draxul-renderer   draxul-font   draxul-grid   draxul-ui
    ↓                                  ↓              ↓
                    draxul-nvim (RPC, process, UI events)
                         ↓
                    draxul-host (NvimHost, TerminalHostBase, ShellHost, …)
                         ↓
                  draxul-app-support (config, keybindings, grid pipeline)
                         ↓
                       app/ (executable orchestrator)
Optional:
draxul-treesitter → draxul-megacity (3D demo, enabled by default)
```

---

## Top 10 Good Things

1. **Strict layered dependency graph.** Libraries only link downward; types has no dependencies, app at top. This prevents circular coupling and enables parallel compilation. The CMake structure enforces it structurally, not just by convention.

2. **Clean interface abstractions for testability.** `IWindow`, `IGridRenderer`, `IHost`, `IGridHandle` are all pure virtual interfaces. The test suite exploits this fully via `FakeRenderer`, `FakeWindow`, and `replay_fixture.h`, enabling component isolation without mocking frameworks.

3. **Comprehensive test suite breadth.** 65+ test files covering not just happy paths but shutdown races (`shutdown_race_tests.cpp`), RPC backpressure (`rpc_backpressure_tests.cpp`), VT parser overflow (`vtparser_overflow_tests.cpp`), corrupt config recovery (`corrupt_config_recovery_tests.cpp`), and atlas overflow (`atlas_overflow_multi_host_tests.cpp`). The coverage of failure modes is unusually good.

4. **RAII throughout.** Smart pointers everywhere, `InitRollback` RAII guard for exception-safe startup, `unique_ptr<IGridHandle>` per host. Ownership is always explicit and single.

5. **Efficient GPU rendering pipeline.** Two-pass instanced rendering (BG quads then FG glyph quads), procedural vertex generation in shaders (no vertex buffers), dirty-cell tracking for incremental GPU writes, and a 2-frame pipeline — all appropriate choices for a grid renderer.

6. **Structured logging with categories and levels.** `LogCategory` enum, per-category filtering, `NDEBUG` stripping of `DEBUG`/`TRACE` calls, file + stderr dispatch, CLI flags (`--log-level`, `--log-file`) that work reliably inside macOS `.app` bundles. This is production-quality observability.

7. **Platform abstraction done properly.** Renderer factory (`renderer_factory.cpp`) creates `MetalRenderer` or `VkRenderer` behind `IRenderer`. Platform-specific code is contained to `src/metal/` and `src/vulkan/` subdirectories. `#ifdef __APPLE__` only appears in the factory and platform-specific source files.

8. **IRenderPass plugin system.** The `register_render_pass()` mechanism lets subsystems inject custom GPU passes (e.g., `CubeRenderPass`) without coupling into the renderer internals. This is a clean extensibility point that replaced a `void*` callback.

9. **Multi-agent review process.** `ask_agent.py` drives Claude/Gemini/GPT reviews, `plans/reviews/` accumulates consensus, and `CLAUDE.md` documents the synthesis workflow. This is a mature approach to AI-assisted code review that treats agents as independent reviewers rather than rubber-stamp approvers.

10. **`GridHostBase` reduces duplication across host types.** All terminal/Neovim hosts share one base class that provides grid, highlight table, cursor blinker, `GridRenderingPipeline`, and viewport management. `NvimHost`, `LocalTerminalHost`, and shell variants are all thin on top.

---

## Top 10 Bad Things

1. **`App` is a god object.** `app.h` shows it owns `window_`, `renderer_`, `text_service_`, `ui_panel_`, `host_manager_`, `gui_action_handler_`, `input_dispatcher_`, and `frame_timer_`, with 17+ private methods. The `initialize()` method is a sequenced waterfall of subsystem init with `InitRollback` holding everything together. Any change to startup order or new subsystem touches this file. It should be split into a `LifecycleCoordinator` + specialized `AppSystems` struct.

2. **GLSL and Metal shaders are manually mirrored.** `grid_bg.vert`, `grid_bg.frag`, `grid_fg.vert`, `grid_fg.frag` (Vulkan) and `grid.metal` (Metal) contain the same cell rendering logic duplicated across two shader languages. There is no validation step confirming they stay in sync — a logic change (e.g., to underline rendering or scroll offset) must be made in both and tested independently. The Megacity cube shader adds a third sync surface.

3. **`IRenderer` uses multiple inheritance as a compatibility shim.** The comment in `renderer.h` explicitly calls it "backward-compat combined interface." `IRenderer : public IGridRenderer, public IImGuiHost, public ICaptureRenderer` conflates unrelated concerns. ImGui lifecycle has no business on a grid renderer interface. The `RendererBundle` wrapper helps at the call site, but `FakeRenderer` in tests still inherits the full combined interface.

4. **`TerminalHostBase` / VT stack is large and hard to boundary-test.** The VT implementation spans `terminal_host_base.cpp`, `terminal_host_base_csi.cpp`, `terminal_sgr.cpp`, `vt_parser.cpp`, `vt_state.h`, `alt_screen_manager.cpp`, `scrollback_buffer.cpp`, `selection_manager.cpp`, `mouse_reporter.cpp`, `terminal_key_encoder.cpp` — about 10 files forming a single large state machine. Testing individual CSI handlers requires exercising `TerminalHostBase` as a whole. A `VtEmulator` value type (state + dispatch, no GPU) would make the logic unit-testable independently.

5. **Raw pointers stored in `GridHostBase` after `initialize()`.** `window_`, `renderer_`, `text_service_`, and `callbacks_` are raw pointers set at init time (`IWindow* window_ = nullptr`). If these owners are destroyed while the host is alive (e.g., in a failure recovery path), the pointers become dangling with no protection. No reference wrappers or lifetime assertions exist.

6. **No thread annotations or assertions on shared state.** The architecture document states "all grid and GPU state is only touched by the main thread" and "reader thread pushes to thread-safe queue." But there are no `THREAD_ANNOTATION`, `std::atomic` counters, or `assert(std::this_thread::get_id() == main_thread_)` calls anywhere visible. A future developer touching `NvimRpc` or `Grid` cannot statically or dynamically verify the threading contract.

7. **MegaCity 3D demo is ON by default in production.** `DRAXUL_ENABLE_MEGACITY=ON` is the default. The spinning-cube demo host and TreeSitter panel compile into every user binary, adding link time, binary size, and surface area (Metal/Vulkan cube pipelines, extra shader compilation) to a production terminal emulator.

8. **`expand_dirty_cells_for_ligatures` allocates a temporary vector every flush.** In `grid_rendering_pipeline.cpp:47`, `std::vector<Grid::DirtyCell> expanded = dirty; expanded.reserve(dirty.size() * 3)` runs on every frame flush for every dirty cell. For active terminal sessions this is called frequently. A pre-allocated buffer as a member of `GridRenderingPipeline` would eliminate these heap allocations on the hot path.

9. **Render tests are Windows-only in practice.** The `DRAXUL_ENABLE_RENDER_TESTS` path and reference BMP comparisons are wired to the Windows build. The macOS Metal renderer has no reference screenshot tests. A Metal rendering regression (Y-flip, atlas upload, scroll offset) could go undetected until a user reports it.

10. **`scroll_speed` validation is split between config loading and comments in `CLAUDE.md`.** The range `(0.1, 10.0]` and the "log WARN and fall back to 1.0" behavior is described in `CLAUDE.md` but the enforcement code sits somewhere in `AppConfig`. If a developer adds another bounded config field, there is no shared "validated scalar" utility to reach for — the pattern must be rediscovered and re-implemented.

---

## Best 10 Features to Add (Quality of Life)

1. **Tab bar / named session management.** A persistent tab strip above panes showing session names (e.g., `nvim ~/project`, `zsh ~/src`). Switchable via `Cmd+1..9` or configurable keybindings. Currently panes are anonymous and switching focus is purely spatial. Would dramatically improve multi-session workflows.

2. **URL detection with click-to-open.** Parse OSC 8 hyperlinks and bare URLs in terminal output; highlight on hover; open via `Cmd/Ctrl+Click`. This is standard in modern terminals (iTerm2, WezTerm, Ghostty) and expected by users who copy links from build output.

3. **Session restore on startup.** Persist and restore open panes, their host types, working directories (from OSC 7), and split ratios to `config.toml` or a session file. Currently every restart requires manually re-opening panes.

4. **Per-pane font size.** Allow individual pane font size overrides (smaller for a monitoring pane, larger for primary editing). The infrastructure exists — `on_font_metrics_changed()` is per-host — but there is no config or keybinding mechanism to trigger it per-pane.

5. **Scrollback search (`Ctrl+F` / `/`).** Text search within the scrollback buffer with highlighted match display and `n`/`N` navigation. Critical for reading build output, logs, and test results without leaving the terminal. Most competitive GUIs have this.

6. **Configurable DPI override.** A `dpi_override` config field that bypasses OS DPI detection. Useful for users on mixed-DPI setups, remote desktop, or when the OS reports incorrect PPI. The `display_ppi_` member already exists in `App`.

7. **Diagnostic panel: font glyph atlas visualizer.** Show the current 2048×2048 atlas texture in the ImGui panel, with overlay showing which cells are used. Helps debug missing glyphs, fallback font selection, and atlas pressure. The atlas is already a `uint8_t*` array accessible to the renderer.

8. **OSC 52 clipboard read support.** Currently clipboard writes via OSC 52 work; reads (responding to OSC 52 `?` queries) are absent or incomplete. Remote `tmux` and SSH sessions rely on this for clipboard access. Completing the read path would make remote Neovim fully usable.

9. **Pane drag-to-reorder.** Allow dragging pane dividers to reorder panes spatially. `SplitTree` has ratio adjustment but no reorder support. This would make managing complex split layouts practical.

10. **Error-resilient config with `--generate-config`.** A CLI flag that writes a fully-annotated `config.toml` with all defaults and comments. Currently there is no template — users must consult `CLAUDE.md` or guess field names. An annotated generated config would reduce support burden.

---

## Best 10 Tests to Add

1. **`GridRenderingPipeline::expand_dirty_cells_for_ligatures` correctness.** There are no direct unit tests for the ligature neighbor-expansion logic. An off-by-one (e.g., expanding past column 0 or past `grid.cols()-1`) causes silent rendering artifacts. Test: dirty cell at col 0, at last col, single-cell grid, adjacent dirty cells.

2. **`SplitTree` minimum-pane size enforcement.** `split_tree_tests.cpp` exists but likely does not cover the ratio clamp behavior when window size falls below minimum pane dimensions. Test: split a pane, then resize the window to < 2× minimum cell size; verify ratios are clamped, not negative.

3. **Font size change cascades through all panes.** Verify that `font_increase` / `font_decrease` via `GuiActionHandler` triggers `on_font_metrics_changed()` on all active hosts (not just the focused one), and that all pane viewports are recalculated. Currently tested per-component but not end-to-end across multi-pane layout.

4. **RPC payload fragmentation at pipe buffer boundary.** `rpc_integration_tests.cpp` exists but may not exercise msgpack messages split across multiple pipe reads at sizes near OS pipe buffer limits (4KB, 16KB, 64KB). A test that sends a 70KB+ `grid_line` batch in 1KB chunks would validate the decoder's stream accumulator.

5. **`AppConfig` full round-trip fidelity.** Serialize a fully-populated `AppConfig` to TOML, reload it, and assert all fields are identical (including float values like `scroll_speed`, nested keybinding tables, and boolean flags). The existing `app_config_tests.cpp` may not cover all fields.

6. **`MetalRenderer` initialization on macOS (headless).** No Metal-side GPU test exists. A test that creates a `MetalRenderer` against a headless `MTLDevice` (available on macOS without a display), calls `create_grid_handle()`, `set_atlas_texture()`, and `set_cell_size()`, and verifies no crash would catch Metal API regressions at the CI level.

7. **`TerminalHostBase` CSI cursor movement boundary conditions.** CSI cursor movement sequences (`CUP`, `CUU`, `CUD`, `CUF`, `CUB`) at grid edges (row 0, col 0, last row, last col) need explicit boundary tests. A cursor positioned at (0,0) receiving CUB should stay at (0,0), not wrap or overflow.

8. **Atlas reset with multiple concurrent grid handles.** `atlas_overflow_multi_host_tests.cpp` exists but verify it covers the case where the atlas is reset mid-frame (due to overflow) while two hosts have pending cell updates. The reset should invalidate all handle caches and force full re-upload without a use-after-free.

9. **`InputDispatcher` prefix-chord timeout.** The existing `inputdispatcher-prefix-stuck-test` covers some cases, but test the case where a prefix key is pressed, then the app loses focus, then regains focus — verify the prefix state is cleared and the next key is treated as a fresh input, not as completion of the previous chord.

10. **Scrollback viewport after terminal resize.** When the terminal is resized (cols/rows shrink), lines in scrollback that exceed new width must be visually wrapped or truncated. Test: fill scrollback with 80-char lines, resize to 40 cols, verify the scrollback viewport reads the correct content without index corruption or out-of-bounds access.

---

## Worst 10 Features

1. **MegaCity 3D spinning cube host.** Enabled by default (`DRAXUL_ENABLE_MEGACITY=ON`). Compiles a spinning-cube 3D renderer into every user binary. It is a prototype/demo feature, not a user-facing terminal feature. The TreeSitter code it uses for "agent analysis" is not integrated into the nvim editing workflow. Belongs behind `OFF` by default or in a separate demo branch.

2. **`LocalTerminalHost` VT100 emulator.** A hand-rolled VT100 terminal emulator (`TerminalHostBase` + VtParser + 10 files) that will always be incomplete compared to libvte or the xterm reference implementation. Maintaining DECSC/DECRC, alt screen, scrollback, SGR, CSI mouse, and all the edge cases is a permanent maintenance burden. For a Neovim GUI, Neovim's own terminal emulator (`:terminal`) is the better investment.

3. **ImGui dockspace overhead with no docking.** `render_app_imgui_dockspace()` in `app.cpp` creates a fullscreen dockspace every frame with `ImGuiDockNodeFlags_PassthruCentralNode`. The diagnostics panel uses this but is not actually dockable into panes. This adds an ImGui node evaluation pass per frame for a feature that is not used in the visible UI.

4. **`sdl_file_dialog.h` open-file integration.** An SDL3 file-open dialog exists (`sdl_file_dialog.h`, referenced in `file_drop_tests.cpp`). Opening files via GUI dialog is not a natural terminal workflow — it circumvents the shell, bypasses `nvim`'s `:e` workflow, and creates a dependency on SDL3's platform-specific file dialog API which is not uniformly implemented across OS versions.

5. **`UiRequestWorker` async abstraction.** `ui_request_worker.h` provides a worker thread for "long-running UI operations." The actual uses (font queries, RPC requests) are all sub-millisecond. The abstraction adds `std::mutex`, `std::condition_variable`, and thread lifecycle complexity for work that could simply run synchronously on the main thread.

6. **`DRAXUL_ENABLE_RENDER_TESTS` compile-time guard in production headers.** The render test infrastructure (`ICaptureRenderer`, `CapturedFrame`, `request_frame_capture()`) is conditionally compiled into the renderer interface. This means the renderer interface changes shape between test and production builds, breaking the fundamental promise of interface stability. Render capture should be a separate injectable capability, not a `#ifdef` on the core renderer interface.

7. **macOS menu bar with partial action coverage.** `macos_menu.h` creates a macOS native menu bar. It exposes a subset of `GuiActionHandler` actions (font changes, clipboard). As new actions are added to `GuiActionHandler`, the menu falls further behind. Users discover features are in keybindings but not menus, or vice versa. Either make the menu data-driven from the actions table, or remove it.

8. **Config unknown-key warning without suggestions.** When `config.toml` contains `scrool_speed = 2.0` (typo), Draxul logs a warning but offers no "did you mean `scroll_speed`?" hint. The TOML key is consumed and silently ignored. The warning exists (`config-unknown-key-warning` work item is complete) but it is not actionable. Users waste time debugging typos.

9. **`startup_steps_` timing telemetry stored in `App`.** `App` accumulates `StartupStep` timing entries during `initialize()` and stores them as `std::vector<StartupStep> startup_steps_`. These are only used by the diagnostics panel. The App class accretes telemetry concerns it should not own. This data belongs in a `DiagnosticsCollector` passed to each subsystem's init.

10. **`PowerShellHost` on Windows as a first-class host.** `powershell_host_win.cpp` exists alongside `shell_host_win.cpp` as a separate host type. PowerShell is just a shell — running it through the same `LocalTerminalHost` VT emulator with a different executable path would suffice, rather than a separate class with its own tests (`powershell_host_tests.cpp`). The duplication will need to be maintained if the VT or key encoding layer changes.

---

*Report generated from direct source analysis of `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/` as of the current working tree on 2026-03-24.*
