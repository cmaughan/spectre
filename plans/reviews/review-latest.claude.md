Here is the full report:

---

# Draxul Codebase Review

*Generated: 2026-03-22. Based on direct file reads of `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/`.*

---

## Top 10 Good Things

### 1. Clean Interface Hierarchy with RendererBundle
The renderer hierarchy (`IBaseRenderer → I3DRenderer → IGridRenderer`) is well-designed. The diamond `IRenderer` interface is wrapped by `RendererBundle`, which provides typed accessors without exposing the diamond inheritance to callers. This pattern prevents accidental coupling and makes platform switching (Metal/Vulkan) transparent to the app layer.

### 2. Comprehensive, Targeted Test Suite (65+ files)
The test coverage is genuinely impressive for a native GUI application. Nearly every library module has dedicated test files: RPC integration and backpressure, grid mutations, VT parser fuzz testing, font fallback corpus, scrollback overflow, host lifecycle races, and render state. The `replay_fixture.h` builders allow reproducing UI parsing bugs without spawning Neovim — a practical, well-designed test-support investment.

### 3. Deterministic Visual Regression Tests
The render snapshot workflow (scenario `.toml` files → renderer backbuffer capture → BMP reference diff → bless commands) is the right solution for a GPU-rendered application. Capturing from the swapchain/drawable rather than the desktop avoids compositor noise and DPI artifacts. The `do.py` entry point makes the workflow accessible without memorising raw command lines.

### 4. Robust Process Management (UnixPtyProcess)
`UnixPtyProcess` demonstrates careful systems programming: a self-pipe for clean reader thread wakeup, multi-phase SIGTERM/SIGKILL with a 100 ms grace period, foreground process group handling via `tcgetpgrp()`, FD_CLOEXEC on all descriptors, SIGPIPE suppression, and login-shell argv convention. This level of care prevents hangs and zombie processes on edge cases.

### 5. Shared Shader Constants (`decoration_constants_shared.h`)
A single header is consumed by both Metal (`.metal`) and GLSL (`.vert`/`.frag`) shader files via `#define` macros written to be valid in both languages. This eliminates the common pitfall of shader magic-number drift between backends. Combined with `static_assert` verification of the `GpuCell` layout, the CPU/GPU boundary is reliably typed.

### 6. PIMPL and Deps Patterns Used Consistently
`TextService`, `NvimProcess`, and `NvimRpc` use PIMPL to hide implementation headers (FreeType, HarfBuzz, MPack) from callers. The `Deps` struct pattern (`InputDispatcher::Deps`, `GuiActionHandler::Deps`, `HostManager::Deps`) provides explicit, testable dependency injection without complex framework machinery. Both patterns are applied consistently rather than selectively.

### 7. `do.py` Central Entry Point
A single root-level `do.py` with a `--help` screen and memorable short commands (`smoke`, `blessall`, `shot`, `test`, `syncboard`) dramatically reduces friction for the safety nets already built. This is especially valuable in an AI-assisted workflow where agents and humans need a shared, discoverable command vocabulary.

### 8. Init Rollback Guard in App::initialize()
`App::initialize()` uses an RAII rollback guard that undoes any partially-completed initialisation on failure. This prevents resource leaks and partially-initialised objects from reaching the main loop — a pattern rarely seen in GUI applications and important for clean error recovery.

### 9. GpuCell `static_assert` Layout Verification
`renderer_state.h` uses `static_assert` to verify the exact byte size (112) and field offsets of `GpuCell` against the shader struct layout. This catches ABI mismatches at compile time rather than producing silent rendering corruption — a strong example of compile-time safety applied to a CPU/GPU data interface.

### 10. Living Engineering Commentary (`docs/learnings.md`)
The learnings file is updated as work happens, capturing cross-cutting lessons (color emoji pipeline requirements, CJK fallback strategy, multi-line TOML parsing pitfalls, Windows command-line quoting hazards, parallel agent integration patterns). This is a practical institutional memory that reduces re-discovery cost for both humans and agents.

---

## Top 10 Bad Things

### 1. ImGui Host Rendering Duplicated Between `pump_once()` and `run_render_test()`
The I3DHost ImGui detection and rendering block (~20 lines) appears identically in both `App::pump_once()` and `App::run_render_test()`. Any change to the logic (e.g., how the active host is found, how ImGui draw calls are issued) must be made in two places. This is a latent divergence bug. Extract a private `draw_host_imgui()` helper.

### 2. ImGui Font Size Formula Duplicated Three Times in `app.cpp`
The expression `static_cast<float>(text_service_.point_size() - 2)` (or equivalent derived from cell metrics) appears three times when computing the ImGui font size. A private helper or a named constant would eliminate the silent divergence risk.

### 3. `SplitTree::find_leaf_node()` is `const` but Returns Mutable `Node*`
The method is declared `const` but returns `Node*` (non-const), which silently discards the const qualifier of `this`. The implementation uses a recursive `std::function<Node*(Node*)>` on `root_.get()`. This is technically undefined behaviour if the returned pointer is ever used for mutation through a const `SplitTree`. It should either return `const Node*` or remove the `const` qualifier from the method.

### 4. Chord Prefix Mode Never Cancelled on Key Release
`InputDispatcher`'s chord prefix state is activated on a specific key-down and is only consumed on the next key-down. If the user presses the prefix key and releases it without pressing a chord key (e.g., prefix activated accidentally), the dispatcher remains in prefix mode silently until the next key event. The comment in the code acknowledges this but leaves it as a known issue. A key-release handler should reset the prefix flag.

### 5. `log_would_emit()` Acquires a Mutex
The logging system acquires a `std::mutex` on every call to `log_would_emit()`. In hot rendering paths, any conditional logging check incurs mutex overhead even when logging is disabled. Consider `std::atomic<LogLevel>` for the active level comparison to make the common "disabled" path lock-free.

### 6. Hardcoded Default Terminal Colors in ShellHost
`shell_host_unix.cpp` hardcodes foreground (`{0.92, 0.92, 0.92}`) and background (`{0.08, 0.09, 0.10}`) colors. These are not read from `config.toml`. A terminal emulator that ignores user color preferences in its default shell host is a usability regression waiting to happen when a user changes their theme.

### 7. `App::pump_once()` is ~110 Lines — Too Large for a Single Function
The frame loop function handles window activation, dead-pane closure, host pumping, ImGui frame lifecycle, dirty rect rendering, host-specific draw, vsync wait, and shutdown transitions sequentially. This makes it difficult to understand the frame budget, test individual stages, or extend the loop with new phases. The stages should be named private helpers.

### 8. Hand-Rolled Windows Command-Line Quoting (Latent Security Bug)
`quote_windows_arg()` in the Windows process spawn code is a hand-rolled implementation of a notoriously tricky algorithm (trailing backslashes before closing quotes, embedded quote escaping). The work item correctly identifies this as both a correctness and an argument-injection risk for user-supplied paths (file drop, config). The preferred fix — using `lpApplicationName` + separately-quoted `lpCommandLine` per MSVC spec — is documented but not yet implemented.

### 9. `Grid::scroll()` is ~125 Lines with Four Inline Direction Cases
The four scroll directions (up/down/left/right) are all implemented inline in a single function. Each direction is a clear `for` loop, but the combined length makes the function hard to read and test in isolation. Extracting `scroll_up()`, `scroll_down()`, `scroll_left()`, `scroll_right()` helpers would also make individual direction tests straightforward.

### 10. `CellText::kMaxLen = 32` with a TODO Comment
The 32-byte inline storage cap for grapheme clusters is a hardcoded limit with an acknowledged `TODO: consider std::string for >32-byte clusters`. Zero-width joiner emoji sequences (e.g., family emoji) can exceed 32 bytes. Truncation is logged as a warning but the glyph is silently corrupted. This is a known correctness hole for a Neovim frontend likely to encounter complex emoji in code comments and markdown.

---

## Best 10 Features to Add (Quality of Life)

### 1. Configurable Terminal Default Colors
Read `fg` and `bg` defaults for `ShellHost` from `config.toml` (a new `[terminal]` section). This aligns the shell pane with the user's preferred color scheme and makes Draxul usable as a general-purpose terminal without hardcoded grey-on-dark defaults.

### 2. Persistent Split Layout (Session Restore)
Serialize the `SplitTree` layout, host kinds, and per-pane working directories to a session file on clean exit. On startup, restore the previous layout. This is the single most-requested feature for terminal multiplexer-style applications and makes Draxul a genuine `tmux` replacement candidate.

### 3. Searchable Scrollback (`/pattern` in Scrollback Mode)
Extend `LocalTerminalHost` scrollback with a simple pattern-match search that highlights matching lines and jumps to the next/previous match. The `scrollback_buffer.h` infrastructure already exists; adding a search index over the stored rows is straightforward.

### 4. URL Detection and Mouse-Clickable Links
Scan rendered terminal lines for URL patterns (`https?://...`, file paths) and underline them on hover. On click (with modifier), open via `xdg-open`/`open`. The decoration layer already supports underline styles; the detection is a post-render pass over the display grid.

### 5. Per-Pane Environment Overrides in Config
Allow `config.toml` to specify environment variable overrides per host kind (e.g., `[hosts.shell] env = {COLORTERM = "truecolor"}`). Currently there is no way to inject `TERM`, `COLORTERM`, or `DRAXUL_VERSION` into the child process environment from config.

### 6. Font Fallback Configuration in `config.toml`
Expose the font fallback chain as a user-configurable list under `[fonts]`. Right now fallback selection is heuristic. A user with a specific CJK font preference has no way to control the order. The existing `FontSelector` / `FontResolver` PIMPL is the right place to wire this.

### 7. `pump_once()` Frame Timing Diagnostics (Debug Overlay)
Add an optional `debug_overlay = true` config flag that renders a small frame-time graph in a corner (last 128 frames, ms/frame, P99 spike marker). The ImGui host overlay infrastructure already exists; this is a few hours of work and permanently improves diagnosability of jank reports.

### 8. Shell Pane Title Bar Reflects CWD
Hook into the OSC 7 escape sequence (`file://hostname/path`) that most modern shells emit on directory change, and use it to update the pane title bar. The VT parser already handles OSC sequences; adding a `on_osc_7` callback to `TerminalHostBase` is the only new plumbing needed.

### 9. Bracketed Paste Confirmation for Large Pastes
When pasting more than N lines (configurable, default 5) into a terminal pane, show a one-line confirmation banner ("Paste 47 lines? [y/n]"). This is a standard safety feature in modern terminal emulators that prevents accidental execution of multi-line clipboard content.

### 10. `config.toml` Live Reload
Watch `config.toml` with a filesystem event (kqueue on macOS, `ReadDirectoryChangesW` on Windows) and apply non-structural settings (colors, keybindings, scroll speed) without restart. Font size and split layout would still require restart. The `AppConfig` serialisation layer already exists; wiring hot-reload is primarily an `INotifyFileChange` abstraction plus `App::apply_config_delta()`.

---

## Best 10 Tests to Add

### 1. `App` Smoke Test with `FakeWindow` + `FakeRenderer`
An end-to-end `App::initialize()` → `pump_once()` × N → `shutdown()` test using the existing `FakeWindow` and `FakeRenderer` stubs. Currently there is no test that exercises the full orchestrator. This would catch startup/shutdown sequencing bugs, host manager lifecycle errors, and frame loop panics that only surface when all subsystems are wired together.

### 2. `InputDispatcher` Prefix-Mode Stuck State
Test that pressing the chord prefix key and then releasing it (without pressing a chord key) correctly resets `prefix_active` to `false` before the next unrelated key-down. This directly covers the known bug in item #4 of the bad list.

### 3. `Grid::scroll()` Per-Direction Unit Tests
Isolated tests for each of the four scroll directions (`up`, `down`, `left`, `right`) covering: single-row scroll, multi-row scroll, scroll at boundary (scroll region start/end), and wide-character boundary repair. The existing `grid_tests.cpp` covers some scroll cases but not the full matrix.

### 4. `SplitTree` Const-Correctness Regression
A test that calls `find_leaf_node()` through a `const SplitTree&` reference and verifies the returned identity matches the expected leaf — this will fail at compile time if the method signature is corrected to return `const Node*` and the callers are not updated, surfacing the fix opportunity.

### 5. Windows `quote_windows_arg()` Edge Cases
Unit tests for:
- paths with spaces (`C:\Program Files\nvim\nvim.exe`)
- paths ending in backslash (`C:\nvim\`)
- paths with embedded double-quotes (hypothetical, must not corrupt command line)
- round-trip: constructed command line parsed by `CommandLineToArgvW` yields original tokens

These tests can run on all platforms if the quoting function is extracted to a pure function in a header.

### 6. `CellText` Truncation at Boundary
Test that storing a grapheme cluster of exactly 32 bytes succeeds, exactly 33 bytes logs a warning and truncates, and the resulting `CellText` does not contain a split UTF-8 sequence (i.e., truncation is clean at a code-point boundary, not mid-sequence).

### 7. `HighlightTable::resolve()` Reverse Video Interaction
Test all combinations of reverse-video with explicit fg/bg override, default fg/default bg, and the normal → visual mode transition. The `resolve()` method handles attribute swapping but the interaction with partial overrides (fg set, bg default) is complex and currently has no targeted test.

### 8. `log_would_emit()` Concurrency Test
A stress test that calls `log_would_emit()` and `log_printf()` from N threads simultaneously for 1000 iterations each, asserting no output corruption or deadlock. This validates the mutex-based log system under contention and would catch the lock-free migration (replacing the mutex with `std::atomic<LogLevel>`) if it is ever attempted.

### 9. `AppConfig` Hot-Reload Delta Application
Test that calling `AppConfig::load()` a second time with a modified `config.toml` (changed `scroll_speed`, changed keybinding) produces a config struct where only the changed fields differ from the first load, and that out-of-range values (`scroll_speed = 0.0`) are rejected with a warning and fall back to the previous valid value.

### 10. Render Scenario: Wide-Character Scroll Boundary Repair
A deterministic render scenario that fills a 40-column grid with CJK characters, scrolls by one line, and asserts that no half-width cells from split wide characters appear in the reference BMP. This directly stress-tests the wide-character boundary repair in `Grid::scroll()` and produces a visual artifact that is immediately obvious in a BMP diff.

---

## Worst 10 Features

### 1. `MegaCityHost` 3D Cube Demo
The spinning cube render pass in `libs/draxul-megacity/` is framed as a "code analysis host" but in practice it is a tech demo with no production utility. It adds a full 3D render pipeline (Metal + Vulkan cube shaders, `CubeRenderPass`, `MegaCityHost`), a TreeSitter panel stub, and a dedicated library module — all for a feature that does not solve any Neovim GUI problem. The presence of this module increases build complexity, shader count, and host-manager code paths. Unless MegaCityHost evolves into a real feature, it should be removed or quarantined behind a CMake option.

### 2. Hand-Rolled BMP Read/Write in `bmp.h/bmp.cpp`
The BMP serialisation library exists solely to support render-test reference images. Using an established single-header library (STB Image) would eliminate ~300 lines of custom BMP code, support more formats, and remove the risk of format-parsing bugs. The `write_bmp_rgba()` function also creates parent directories, mixing I/O concerns with format logic.

### 3. `(void)pixel_h; (void)logical_h;` Suppression Comments (×5 in `app.cpp`)
Using `(void)var` to suppress unused-variable warnings from structured bindings is a code smell that indicates the bindings should be decomposed differently (only bind what is used, or use `std::ignore`). Five occurrences in a single file suggest the structured binding style is being used in places where a simpler access pattern would be cleaner.

### 4. `app_config.h` Monolith (~400 lines)
`AppConfig` combines config struct definition, TOML parse logic, file I/O, serialization, `AppConfigOverrides` merge, and `GuiKeybinding` chord parsing — all in one header. This makes it impossible to use any of these subsystems independently and causes recompilation of all dependents when any config field changes. Splitting into `app_config_types.h`, `app_config_io.cpp`, and `keybinding_parser.cpp` would reduce coupling.

### 5. Linear String Dispatch in `GuiActionHandler::execute()`
The `execute()` method uses an `if-else` chain of `action == "toggle_diagnostics"`, `action == "copy"`, etc. This is fine at 9 actions but grows linearly and is easy to mistype. A `static const std::unordered_map<std::string_view, std::function<void()>>` built once at construction would be O(1), self-documenting, and would produce a compile-time warning if a handler is accidentally omitted.

### 6. `UiEventHandler` Not Split from `NvimInput` in `nvim_ui.h`
Both `UiEventHandler` (processes Neovim → app redraw events) and `NvimInput` (translates app → Neovim key events) live in the same header. These are opposite data-flow directions with no shared state. Splitting them into `nvim_ui_handler.h` and `nvim_input.h` would make their responsibilities clearer and allow tests to include only the direction they exercise.

### 7. `TextInputEvent` Holds a Raw `const char*`
The `TextInputEvent` struct stores a raw pointer to SDL's internal buffer. The lifetime guarantee is implicit (valid until the next SDL event) and is not documented in the struct definition. A misuse — storing the event and processing it one frame later — would produce silent use-after-free. At minimum, the struct should have a `std::string` copy or a `std::string_view` with a lifetime comment.

### 8. Render Scenario TOML Parser is a Custom Ad-Hoc Implementation
`app/render_test.cpp` implements a hand-rolled TOML-like parser for scenario files (including the multi-line array fix documented in `learnings.md`). The project already has TOML parsing machinery in `app_config.h`. Using the same parser for both config and render scenarios would eliminate the custom parser, fix multi-line array handling by default, and close the documented regression category.

### 9. `startup_resize_state.h` as a Separate Type
`StartupResizeState` is a small state machine (~30 lines) that exists only to defer a resize acknowledgement until the first Neovim flush. Its logic could live directly in `GridHostBase` as a two-field state enum without a dedicated header and source file. The current separate-type approach adds a header include and a named object member for what is effectively 3 states and 2 transitions.

### 10. `for_each_host()` Using `std::function<void(LeafId, IHost&)>`
`HostManager::for_each_host()` accepts a `std::function` callback. This allocates a heap closure object on every call. The function is called per-frame for each host to perform updates. Using a template parameter (`template<typename F> void for_each_host(F&& fn)`) would make the callback zero-cost and is a straightforward change with no API impact at the call sites (the lambdas at the call sites are unchanged).

---

*End of report.*
