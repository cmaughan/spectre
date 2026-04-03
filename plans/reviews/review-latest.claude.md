# Draxul Comprehensive Code Review

## Per-Module Observations

### app/ — The Orchestrator Layer

**Files:** `app.h/.cpp`, `main.cpp`, `input_dispatcher.h/.cpp`, `split_tree.h/.cpp`, `host_manager.h/.cpp`, `gui_action_handler.h/.cpp`, `command_palette.h/.cpp`, `command_palette_host.h/.cpp`, `diagnostics_panel_host.h/.cpp`, `frame_timer.h`, `fuzzy_match.h`

**Strengths:**
- Clean dependency injection via `Deps` structs on nearly every component (`InputDispatcher::Deps`, `HostManager::Deps`, `GuiActionHandler::Deps`, `AppDeps`). This makes every component testable in isolation without mocking frameworks.
- `SplitTree` is a self-contained binary-tree data structure with no external dependencies on rendering, hosting, or config. Pure algorithmic code.
- The `RenderTestPhase` state machine in `app.cpp` is a clean enum-based design with a named context struct and clear phase transitions.
- `InitRollback` RAII guard in `App::initialize()` ensures the app shuts down even if initialization fails partway.
- `CommandPalette` and `DiagnosticsPanel` are properly implemented as `IHost` instances, giving them consistent lifecycle management.

**Issues:**
- **`App::wire_gui_actions()` is a 100-line lambda factory.** Every GUI action is wired via a lambda closure capturing `this`. Dense and error-prone — adding a new action requires touching four places: `Deps`, `GuiActionHandler` method, static `action_map()`, and `wire_gui_actions()`.
- **`GuiActionHandler::Deps` has 19 `std::function` fields.** Approaching the point where the Deps pattern works against readability. A registration-based approach would scale better.
- **`InputDispatcher` mouse handler code is repetitive.** Three handlers (`on_mouse_button_event`, `on_mouse_move_event`, `on_mouse_wheel_event`) repeat the same pattern: forward to UiPanel, check `wants_mouse()`, check `contains_panel_point()`, scale coordinates, forward to host.
- **ImGui font size formula duplicated three times** (`App::initialize()`, `App::apply_font_metrics()`, `App::initialize_host()`). The expression `float(cell_height) * (point_size - 2) / point_size` includes an unexplained magic number `2`.
- **`host_owner_lifetime_` is `std::shared_ptr<int>(0)`.** The sentinel value `0` is never read. This would be clearer as a named type or at minimum documented.
- **`types.h` has a duplicate `#include <glm/glm.hpp>`** on two lines.

---

### libs/draxul-types/ — Header-Only Shared Types

**Files:** `types.h`, `events.h`, `input_types.h`, `highlight.h`, `log.h`, `thread_check.h`, `perf_timing.h`

**Strengths:**
- `ModifierFlags` aligns SDL3 modifier constants directly, avoiding conversion at runtime.
- `MainThreadChecker` is a zero-cost debug-only utility that compiles to nothing in release.
- `HlAttrHash` uses the boost hash-combine pattern correctly and covers all fields.
- The style flags (`STYLE_FLAG_BOLD`, etc.) match the shader constants in `decoration_constants_shared.h` — a single source of truth for shader/CPU flag values.
- `DRAXUL_LOG_DEBUG`/`DRAXUL_LOG_TRACE` strip in release builds.

**Issues:**
- **`Color` is a type alias for `glm::vec4`, not a distinct type.** Any `glm::vec4` (e.g. a position or UV coordinate) can be silently passed where a `Color` is expected. A strong type would catch misuse at compile time.
- **`kAtlasSize = 2048` is defined in `types.h`** but the actual atlas size is configurable. This constant is misleading — it is a default, not a compile-time invariant.
- **`CellUpdate` uses plain `int col, row`** with no validation. Negative values or out-of-range cells would pass silently.

---

### libs/draxul-grid/ — 2D Cell Grid

**Files:** `grid.h`

**Strengths:**
- `CellText` is a fixed-size inline buffer (32 bytes) with UTF-8-aware truncation. Avoids heap allocation per cell.
- UTF-8 validation in `detail::utf8_valid_prefix_length` is thorough — handles 2/3/4 byte sequences with proper overlong and surrogate checking.
- `IGridSink` interface allows the grid to be driven by different hosts without coupling.
- Dirty-cell tracking with a parallel `dirty_marks_` vector and `dirty_cells_` list is efficient for incremental rendering.

**Issues:**
- **UTF-8 parsing inlined in `grid.h`.** Over 100 lines of `detail::` functions are in a header included by many translation units. Increases compile time unnecessarily; these belong in a `.cpp` or dedicated utility header.
- **`CellText::kMaxLen = 32` with a `TODO` for `std::string`.** The ZWJ emoji family sequence (25 bytes) barely fits. Two concatenated sequences would be truncated silently.

---

### libs/draxul-host/ — Terminal Hosting Layer

**Files:** `vt_parser.h`, `vt_state.h`, `selection_manager.h`, `terminal_sgr.h`, `terminal_key_encoder.h`, `mouse_reporter.h`, `alt_screen_manager.h`, `clipboard_util.h`

**Strengths:**
- `VtParser` is a clean character-at-a-time state machine with bounded buffers (`kMaxPlainTextBuffer = 64K`, `kMaxCsiBuffer = 4K`, `kMaxOscBuffer = 8K`). This prevents OOM from pathological terminal output.
- `SelectionManager` is fully decoupled from rendering via callbacks (`set_overlay_cells`, `get_cell`, `grid_cols`, `grid_rows`, `request_frame`).

**Issues:**
- **`SelectionManager::kSelectionMaxCells = 8192`** is a hard limit. On a 200-column terminal, this is only ~40 rows. No user-visible warning is given when the limit is hit.
- **`VtState` is a plain struct with no save/restore methods.** The saved cursor (DECSC/DECRC) is a pair of raw fields (`saved_col`, `saved_row`) that callers manually copy. A `save()` / `restore()` method pair would be cleaner.

---

### libs/draxul-font/ — Font Pipeline

**Files:** `text_service.h`, `font_metrics.h`

**Strengths:**
- Pimpl idiom keeps FreeType/HarfBuzz dependencies out of the public header.
- `IGlyphAtlas` interface allows the text service to be faked in tests.
- Clear constants (`DEFAULT_POINT_SIZE = 11.0f`, `MIN_POINT_SIZE = 6.0f`, `MAX_POINT_SIZE = 72.0f`).

**Issues:**
- **No centralized atlas upload path.** Atlas upload logic exists in `CommandPaletteHost::flush_atlas_if_dirty()` and the main grid render path. A new host needing glyph rasterization must re-implement the same dirty-rect-to-staging-buffer copy. This should be a `TextService` or `IGridRenderer` responsibility.
- **`atlas_dirty()` + `clear_atlas_dirty()` leaks internal lifecycle responsibility** to callers. This should be encapsulated.

---

### libs/draxul-config/ — Configuration

**Files:** `app_config.h`, `app_config_io.h`, `keybinding_parser.h`, `app_options.h`, `config_document.h`, `toml_support.h`

**Strengths:**
- `app_config.h` is a facade header with clear guidance on which narrower headers to prefer.
- `ConfigDocument` separates TOML round-trip fidelity from the typed `AppConfig` struct. User comments and unknown keys are preserved on save.
- Config schema is well-documented inline.

---

### libs/draxul-renderer/ — GPU Rendering

**Strengths:**
- Renderer hierarchy (`IBaseRenderer -> I3DRenderer -> IGridRenderer`) is well-layered.
- `IRenderPass` / `IRenderContext` abstraction allows subsystems to register custom render passes without knowing the backend.
- `decoration_constants_shared.h` is the single source of truth for style flag values shared between CPU and shader code.

**Issues:**
- **The Cell struct is defined three times**: `grid_bg.vert`, `grid_fg.vert`, and `grid.metal`. Any field change must be applied to all three plus the CPU-side layout. This is the highest structural risk in the codebase.
- **`dynamic_cast<MegaCityHost*>` in `host_manager.cpp`** (behind `#ifdef DRAXUL_ENABLE_MEGACITY`) breaks the host abstraction boundary. The `I3DHost` interface should expose `set_continuous_refresh_enabled()` and `set_ui_panels_visible()` as virtual methods.

---

### shaders/

**Strengths:**
- `decoration_constants_shared.h` works in both GLSL and Metal.
- Instanced rendering (generating quads from vertex index) is efficient and avoids vertex buffers.
- Clear BG/FG pass separation.

**Issues:**
- **The 6-vertex quad offset array is hardcoded in every vertex shader** — `grid_bg.vert`, `grid_fg.vert`, BG Metal vertex, FG Metal vertex. Four copies of the same array. A shared include would eliminate this.
- **`PushConstants` / uniform layout is defined separately** in each GLSL vertex shader and again in Metal. Another divergence risk.

---

### tests/

**Strengths:**
- 80 test files with strong coverage across grid, split tree, config, VT parser, font, RPC, keybinding, input dispatch, rendering state.
- Test support infrastructure (`fake_window.h`, `fake_glyph_atlas.h`, `fake_rpc_channel.h`, `replay_fixture.h`, `temp_dir.h`, `scoped_env_var.h`) is well-organized.
- Fuzz tests for VT parser and MPack.
- The render test framework with TOML scenario files and platform-specific BMP references is mature.

**Issues:**
- **No unit tests for the `RenderTestPhase` state machine** in `app.cpp` (tracked as work item 04). The five-phase state machine has no tests for phase transitions, timeout behavior, or edge cases.
- **No dedicated tests for `CommandPalette` fuzzy match scoring** — match position correctness and score ordering deserve their own test file.

---

## Top 10 Good Things

1. **Consistent dependency injection via `Deps` structs.** Every major component uses an explicit dependency bundle. This makes the codebase highly testable without mocking frameworks and enables components to be worked on in isolation.

2. **Strong test coverage.** 80 test files with ~24,000 lines. Fuzz testing for VT parser and MPack. Platform-specific render test infrastructure with TOML scenarios and BMP comparison. Well above average for a C++ project of this size.

3. **Clean library layering with no circular dependencies.** The dependency graph flows strictly downward through `draxul-types -> grid/font/renderer/window -> nvim/host -> app-support -> app`. Libraries link only downward.

4. **Shader constant single source of truth.** `decoration_constants_shared.h` is valid in both GLSL and Metal, and style flag values match the C++ `STYLE_FLAG_*` constants. This eliminates a common class of shader/CPU mismatch bugs.

5. **Robust VT parser design.** Bounded buffers prevent OOM. Explicit state machine with clear states (`Ground/Escape/Csi/Osc/OscEsc`). Pure parsing with callbacks — no coupling to grid or rendering.

6. **`InitRollback` RAII guard.** `App::initialize()` uses a scoped rollback object that calls `shutdown()` on partial init failure. This prevents resource leaks.

7. **Well-structured work items.** Each item in `plans/work-items/` has a problem statement, investigation steps, test design, and acceptance criteria. Machine-parseable naming convention. Excellent for multi-agent/multi-developer workflows.

8. **`CellText` inline buffer with UTF-8-aware truncation.** Avoiding heap allocation per cell while correctly handling multi-byte UTF-8 boundaries is non-trivial and done correctly.

9. **`PERF_MEASURE()` instrumentation throughout.** Every non-trivial function is instrumented. `RuntimePerfCollector` provides live per-function timing with zero cost when disabled.

10. **Config round-trip fidelity.** `ConfigDocument` preserves user comments and unknown TOML keys on save. Typed `AppConfig` and serialization-faithful `ConfigDocument` are properly separated.

---

## Top 10 Bad Things

1. **`GuiActionHandler::Deps` has 19 `std::function` fields.** Adding one GUI action requires touching four places. Does not scale, and every new action is a parallel-development conflict point.

2. **ImGui font size formula duplicated three times with an unexplained magic number.** The expression `float(cell_height) * (point_size - 2) / point_size` appears in three methods. The `2` is undocumented.

3. **Cell struct defined in three shader files independently.** `grid_bg.vert`, `grid_fg.vert`, and `grid.metal` each contain the full 112-byte Cell layout. Any field change must be applied to all three plus the CPU side.

4. **UTF-8 parsing inlined in `grid.h`.** Over 100 lines of `detail::` helper functions in a widely-included header. This inflates compile time for every translation unit that touches the grid.

5. **`InputDispatcher` mouse handler triplication.** The same 7-step pattern is repeated in three handlers. A private `dispatch_mouse_to_host()` helper would make this 5 lines each.

6. **`dynamic_cast<MegaCityHost*>` in `host_manager.cpp`.** Two downcasts behind `#ifdef DRAXUL_ENABLE_MEGACITY` break the host abstraction. The capability should be expressed via `I3DHost` virtual methods.

7. **`App::wire_gui_actions()` is the highest-conflict function in the codebase.** Any feature adding a GUI action must touch this 100-line function of nothing but lambda assignments. Multiple agents working on new features will conflict here constantly.

8. **`Color` is a type alias for `glm::vec4`, not a distinct type.** Positions, UVs, and colors are all the same type. Any `glm::vec4` can be silently passed as a color.

9. **`types.h` has a duplicate `#include <glm/glm.hpp>`.** Minor but signals the file has been modified without careful review.

10. **No centralized atlas upload path.** Each host re-implements the dirty-atlas-to-GPU upload logic. Adding a new host type that renders glyphs requires re-implementing this plumbing.

---

## Top 10 Quality-of-Life Features to Add

1. **Config validation with user-visible error messages.** Invalid config values currently log a WARN silently. A startup toast or statusbar message ("Invalid font_size in config.toml — using default 11pt") would help users debug config issues without reading logs.

2. **Session save/restore.** Saving the split layout and host types to a file and restoring them on next launch. Users who configure specific pane arrangements must re-do them manually every launch.

3. **Mouse-drag divider resizing with visual feedback.** SplitTree supports `set_divider_ratio()` and InputDispatcher detects divider hits, but there is no cursor-change on hover or drag feedback. The 4-pixel hit zone is invisible to users.

4. **Status bar showing current host and pane layout.** A lightweight footer showing "nvim | 2 panes | 80×24" would reduce the need to open the diagnostics panel just to check what is running.

5. **Tab completion in the command palette with argument hints.** The palette accepts `split_vertical zsh` syntax but this is not discoverable. Showing available host names after typing the action name would close the discoverability gap.

6. **Double/triple-click selection.** Word-select (double-click) and line-select (triple-click) are standard terminal emulator behaviors. Currently only click-and-drag is supported.

7. **Copy-on-select.** Many terminal emulators copy to clipboard on mouse-up. Draxul requires explicit `Ctrl+Shift+C`. This breaks the workflow of users coming from xterm, iTerm2, or Alacritty.

8. **Configurable ANSI color palette.** The `[terminal]` config section supports only `fg` and `bg`. The 16 ANSI colors (`color0`–`color15`) are not configurable, blocking users from setting their preferred terminal color scheme.

9. **Font fallback chain display in diagnostics.** When a glyph falls through to a fallback font or fails entirely, the diagnostics panel should show which font was used and how many fallback lookups occurred per frame.

10. **Config file syntax error line numbers.** When `config.toml` has a parse error, the current error message omits the line number. TOML parse errors include position information that should be surfaced to the user.

---

## Top 10 Tests to Add for Stability

1. **`RenderTestPhase` state machine unit tests.** The five-phase state machine (`kWaitingForContent` through `kCapturing`) has no unit tests. Phase transitions, timeout behavior, and edge cases (content lost during settle, diagnostics panel timing) should be tested with a fake clock and fake renderer.

2. **`InputDispatcher` chord binding edge cases.** Test: prefix key pressed, then modifier-only key (should not cancel prefix); prefix key pressed, then unrelated key (should cancel prefix and forward both). The current code has a comment "For now just keep prefix_active_ until the next key-down" which signals incomplete behavior.

3. **`CommandPalette` fuzzy match scoring correctness.** Test that exact matches score higher than substring matches, shorter targets win ties, and match positions are correct for highlighting. The `fuzzy_match.cpp` implementation should have its own test file.

4. **`HostManager` zoom + close interaction.** Test: zoom a pane, then close the zoomed pane. Test: zoom a pane, then close a different pane. Verify viewport recomputation is correct in each case.

5. **`SplitTree` with zero-dimension window.** Test `reset(0, 0)` and `recompute(0, 0)`. The code uses `std::max(0, w - div_w)` but a zero-dimension window may still produce unexpected viewport calculations downstream.

6. **Atlas overflow during multi-host rendering.** When two hosts rasterize glyphs simultaneously and the atlas fills up, the reset should not lose glyphs from one host while updating the other. The interaction with `CommandPaletteHost::flush_atlas_if_dirty()` especially needs coverage.

7. **Config reload with active split panes.** Test: load config, split into 3 panes, change font size via `reload_config()`, verify all three panes receive new font metrics. The `for_each_host()` call in reload is not tested with multiple live panes.

8. **VT parser partial-sequence boundary spanning.** Test: feed the first byte of a CSI or OSC sequence in one `feed()` call and remaining bytes in the next. The parser should resume correctly from `Csi` or `Osc` state across feed boundaries.

9. **`DiagnosticsPanelHost` visibility toggle during render test.** The render test enables diagnostics mid-run. Test that `last_render_time()` advances correctly and the `EnablingDiagnostics` → `SettlingForCapture` transition fires only after the panel has actually rendered.

10. **Concurrent host shutdown and event dispatch.** Test: host process exits between `pump()` and `close_dead_panes()`. The input dispatcher's host pointer must not be a dangling pointer. Verify `set_host(nullptr)` is called before any pane is destroyed.

---

## Top 10 Worst Existing Features

1. **Selection limited to 8,192 cells with no user feedback.** On a 200-column terminal this is only ~40 rows. Users selecting large code blocks are silently truncated. The limit should be raised or a visible warning shown.

2. **No OSC 52 clipboard read support.** The terminal supports clipboard write but not read. Programs that use OSC 52 to read the clipboard (tmux, neovim clipboard integration in remote sessions) fail silently.

3. **No hyperlink support (OSC 8).** Modern CLI tools (ls, git, cargo) emit OSC 8 hyperlinks. Draxul silently strips or misrenders them.

4. **No double/triple-click selection.** Click-and-drag selection exists but word-select and line-select are absent. This is a baseline terminal usability expectation.

5. **No copy-on-select.** Requires explicit `Ctrl+Shift+C` after selection. Breaks the muscle memory of users from xterm, iTerm2, Alacritty, or any X11 terminal.

6. **ANSI color palette not configurable.** Only `fg` and `bg` are configurable in `[terminal]`. The 16 ANSI named colors cannot be themed. Users cannot apply their preferred color scheme.

7. **No named launch profiles.** Users who want different host configurations for different workflows must use CLI flags every launch. Named profiles in `config.toml` would be far more practical.

8. **No toast notifications for runtime errors.** When config reload fails, font loading fails, or a host crashes, the only feedback is a log line. Users not watching the log have no idea why something changed.

9. **No scroll position indicator.** Terminal hosts with scrollback buffer have no visual indicator of scroll position (scrollbar or "N lines above" status). Users cannot tell how far they have scrolled.

10. **MegaCity feature descriptions in `docs/features.md` are impenetrable.** Entries like "MegaCity dependency routing" are 68–92-word technical implementation sentences. A user reading the feature list cannot understand what these features do or why they would want them.
