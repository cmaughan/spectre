# DRAXUL CODE REVIEW REPORT

## Executive Summary

Draxul is a sophisticated, multi-agent-authored Neovim GUI frontend with cross-platform GPU rendering (Vulkan/Metal), terminal emulation, and an innovative 3D code city visualization. The codebase spans ~44K lines of C++20 across 18 modular libraries, with 82 test files containing 937 test cases. The project demonstrates strong architectural discipline, comprehensive testing, and agent-friendly organization, though some areas show signs of rapid agentic development that could benefit from consolidation.

---

## MODULE-BY-MODULE ANALYSIS

### `libs/draxul-types` (Header-Only)
- **Strengths**: Clean separation of POD types, logging abstractions, and utility functions
- **Issues**:
  - Large header files (`unicode.h` 506 lines, `perf_timing.cpp` 387 lines)
  - Complex bitwise operations in Unicode validation could be extracted to a separate validation module
  - Thread checking macros mixed with core types

### `libs/draxul-window` (SDL3 Wrapper)
- **Strengths**: Good abstraction layer over SDL3; platform detection is clean
- **Issues**:
  - `sdl_window.cpp` (516 lines) with complex DPI calculation logic inlined
  - Extensive platform-specific `#ifdef` chains for Windows/macOS/Linux DPI detection
  - No separate DPI calculator utility; logic is duplicated per platform

### `libs/draxul-renderer` (GPU Backend)
- **Strengths**: Clean public interface (`IBaseRenderer` → `I3DRenderer` → `IGridRenderer` hierarchy)
- **Issues**:
  - `megacity_render_vk.cpp` at ~4,878 lines is unmaintainable as a single file
  - Tight coupling between grid rendering and MegaCity 3D rendering
  - `dynamic_cast<I3DHost*>` in `HostManager` breaks interface purity
  - Metal and Vulkan implementations share no common backend utilities; duplicated buffer management patterns

### `libs/draxul-font` (FreeType + HarfBuzz)
- **Strengths**: Clean text pipeline with font fallback chain; ligature support
- **Issues**:
  - Font resolver (388 lines) handles multiple concerns: loading, style detection, fallback chain
  - Glyph cache (384 lines) mixes atlas management with caching logic
  - No separation between font selection caching and ligature analysis caching
  - `font_choice_cache_limit` hardcoded in multiple places

### `libs/draxul-grid` (Cell Grid Model)
- **Strengths**: Excellent UTF-8 validation in `detail` namespace; solid attribute cache design
- **Issues**:
  - TODO in `grid.h`: "consider `std::string` for >32-byte clusters instead of truncating" — currently silently drops data
  - No benchmarks for grid mutation performance on large scrollback
  - Dirty cell tracking lacks range coalescing

### `libs/draxul-host` (Host Abstraction)
- **Strengths**: Clean interface hierarchy; good separation of terminal emulation into base classes
- **Issues**:
  - `terminal_host_base_csi.cpp` (587 lines) handles VT escape sequence parsing as a monolith
  - Mouse reporting (151 lines) tightly coupled to terminal state
  - Heavy `#ifdef` platform branching for PTY vs ConPTY differences
  - `NvimHost` (427 lines) mixes RPC communication with UI event handling

### `libs/draxul-nvim` (Neovim Integration)
- **Strengths**: Thread-safe RPC transport with backpressure handling; message timeout management
- **Issues**:
  - `rpc.cpp` (421 lines) mixes message routing with response correlation
  - `ui_events.cpp` (486 lines) is a massive state machine for Neovim UI redraw parsing with minimal test coverage for partial or out-of-order sequences
  - No version abstraction over the Neovim msgpack UI protocol

### `libs/draxul-megacity` (3D Code City)
- **Strengths**: Ambitious visualization of code as interactive city; TreeSitter integration for semantic analysis
- **Issues**:
  - **CRITICAL**: `megacity_host.cpp` (~2,293 lines) handles initialization, UI, rendering, input, and analytics in one class
  - `megacity_render_vk.cpp` (~4,878 lines) — should be split into multiple renderer backend files
  - `semantic_city_layout.cpp` (~1,772 lines) has no unit tests and depends on TreeSitter at runtime
  - Tight coupling between visualization, code analysis, and performance metrics
  - Live metrics collection lacks documented thread safety

### `libs/draxul-config` (Configuration)
- **Strengths**: TOML parser with rollback safety; keybinding parser with chord support
- **Issues**:
  - `app_config_io.cpp` (602 lines) handles I/O, validation, migration, and defaults in one place
  - `toml_support.h` (165 lines) mixes macros with inline implementations
  - No schema validation layer separate from parsing

### `libs/draxul-ui` (ImGui Wrapper)
- **Strengths**: Metrics panel is polished; input translation is clean
- **Issues**:
  - `ui_panel.cpp` (435 lines) mixes layout, input handling, and rendering
  - Panel styling code duplicated between diagnostics and MegaCity panels
  - ImGui context lifecycle was recently fixed (hosts now own it) but global state assumptions may remain elsewhere

### `app/` (Main Application)
- **Strengths**: Thin orchestration layer; good use of dependency injection (`AppDeps`)
- **Issues**:
  - `app.cpp` initialization is complex and contains a render-test state machine inline
  - `host_manager.cpp` contains two `dynamic_cast<MegaCityHost*>` calls (type coupling leak)
  - Input dispatch chain (key → GUI action → host) is complex and hard to trace

---

## Multi-Agent Development Friction Points

1. **Monolithic MegaCity files** — 2K+ / 4.8K+ line files are nearly impossible to distribute to separate agents without merge conflicts
2. **Interface versioning missing** — Updating `IHost` requires coordinated changes across all host implementations
3. **No clear ownership** — Large files have no code owner markers
4. **Shader compilation is opaque** — Errors buried in CMake build log; agents can't iterate shaders in isolation
5. **Implicit CMake dependencies** — Adding a library dependency requires all agents to rebuild

---

## TOP 10 GOOD THINGS

1. **Modular Library Architecture** — 18 independent libraries with well-defined public APIs; tests depend on individual libraries without dragging in all dependencies
2. **Comprehensive Test Suite** — 82 test files, 937 test cases, covering config, terminal emulation, grid rendering, RPC backpressure, font fallback, keyboard/mouse, render snapshots, and shutdown races
3. **Dependency Injection Pattern** — `AppDeps`, `HostManager::Deps`, `InputDispatcher::Deps` allow fakes to be injected without modifying production code — excellent for multi-agent development
4. **Consistent Error Handling** — No exceptions in production paths; errors bubble via return values with string messages; failure reasons stay visible
5. **Cross-Platform Abstraction** — `IWindow`, `IRenderer`, `IHost` successfully hide Windows/macOS differences; platform-specific code isolated to `src/` impl files
6. **Logging Infrastructure** — Structured logging with categories, levels, file/console routing; `DRAXUL_LOG_DEBUG()` macros only compile in debug builds; CLI flags for log control
7. **Smart Pointer Discipline** — Extensive `unique_ptr`/`shared_ptr` usage; minimal raw pointers; non-owning pointers clearly marked as such
8. **Font Pipeline Polish** — FreeType + HarfBuzz with ligatures, emoji (ZWJ, variation selectors), wide chars (CJK), fallback chains, and bold/italic/bold-italic variants all working
9. **Render Test Infrastructure** — Pixel-diff snapshot testing with tolerance controls, platform-specific reference images, bless commands — catches visual regressions early
10. **Performance Instrumentation** — `PerfTiming` macros throughout; startup profiling; frame timing; MegaCity live perf overlays with heatmaps for profiling execution patterns

---

## TOP 10 BAD THINGS / PROBLEMS

1. **Monolithic MegaCity Files** — `megacity_host.cpp` (~2,293 lines) + `megacity_render_vk.cpp` (~4,878 lines) = ~7,171 lines in two files; should be split by responsibility (scene building, input handling, rendering, metrics). No unit tests for `semantic_city_layout.cpp` (~1,772 lines).

2. **Type Coupling via `dynamic_cast`** — `HostManager` contains `dynamic_cast<MegaCityHost*>` twice. This breaks interface abstraction; MegaCity should register renderer-specific behavior via an interface method, not be downcast at runtime.

3. **VT Escape Sequence Parser Monolith** — `terminal_host_base_csi.cpp` (587 lines) is a state machine with no separate parser object. Tests must instantiate a full `TerminalHostBase`. Edge-case coverage and mutation testing are both impractical.

4. **Neovim UI Event Parsing State Machine** — `ui_events.cpp` (486 lines) decodes redraw events (`mode_info_set`, `hl_attr_define`, `grid_line`, etc.) with no formal error handling for out-of-order events or missing initialization. Changes to Neovim's UI protocol could silently break compatibility.

5. **DPI Logic Scattered Across Platforms** — SDL/CoreGraphics/GetDeviceCaps DPI logic inlined in `sdl_window.cpp` (516 lines total). No separate utility or unit tests for DPI edge cases; multi-monitor corner cases are hard to reason about.

6. **Config Monolith** — `app_config_io.cpp` (602 lines) does parsing, validation, defaults, and migration in one file. TOML macros pollute the namespace. Keybinding and config parsers should share a validation layer.

7. **Font Style Detection via Filename Pattern Matching** — `font_resolver.cpp` detects bold/italic by checking for `*Bold*`/`*Italic*` in the filename. If fonts use different naming conventions, bold/italic variants silently fail. No explicit API to register which file is the bold variant.

8. **Renderer Backend Duplication** — Metal and Vulkan renderers duplicate atlas management, buffer update patterns, and viewport handling. Despite ~80% code similarity, there are no shared backend utilities. Adding a third backend would require another full copy.

9. **Threading Model Undocumented** — `std::mutex` and `std::atomic` usage throughout `NvimRpc` but no threading model documented at the module level. Main thread mutation of grid state happens without explicit locking — requires careful reading to verify correctness.

10. **Silent Failure for Missing Dependencies** — MegaCity fails silently if TreeSitter grammar files are missing (just logs `LCOV file not found`). No clear user-facing error messages. Font fallback chain doesn't report which fonts failed when all fallbacks are exhausted.

---

## BEST 10 FEATURES TO ADD (Quality of Life)

1. **Config Hot-Reload** — File watcher for `config.toml`; reload keybindings, font, and colors on save without restarting. Most requested QoL for iterative config tuning.

2. **Shader Hot-Reload** — Recompile SPIR-V (Vulkan) or metallib (Metal) from source on change and reload the pipeline without restarting. Critical for visual effect iteration.

3. **Input Event Recorder/Playback** — Record SDL input events to JSON, replay deterministically for regression testing. Extends existing render-test infrastructure. Would eliminate flaky manual repro steps.

4. **Unified Error Toast** — Show recoverable errors (font not found, config parse failure, shader compile failure) as in-app toast/modal rather than console-only. Non-technical users don't watch logs.

5. **Performance Profiler UI Panel** — Leverage existing `PerfTiming` infrastructure to show an in-app flame graph of per-frame costs (grid update, RPC drain, ImGui, render). Add `--perf-capture` flag to save binary profile for offline analysis.

6. **Incremental/Lazy Asset Loading** — Defer MegaCity, TreeSitter grammar, and heavy font variants until first use. Reduces startup time for pure terminal usage.

7. **Config Schema Validation** — JSON Schema (or equivalent) for `config.toml`. Catch unknown keys at parse time instead of silently ignoring them. Generate CLI help text from schema.

8. **Native Theme Integration** — Detect macOS Dark/Light mode changes (via `NSAppearance` notifications) and sync Neovim background/colorscheme. On Windows, hook system accent color changes.

9. **Benchmark Suite with CI Regression Gating** — Automated benchmarks for grid mutation (cells/sec), font rasterization (glyphs/sec), RPC roundtrip latency, and frame time. Alert (fail CI) if any metric regresses >10% vs. baseline.

10. **Split Layout Persistence** — Save and restore the pane split layout (positions, hosts, file paths) across restarts via a session file. Currently all layout is ephemeral.

---

## BEST 10 TESTS TO ADD (Stability)

1. **Rapid Window Resize Stress Test** — Resize window 50 times in 100ms; verify no buffer overflows, correct final viewport, no renderer/ImGui deadlocks. Existing resize tests likely don't cover pathological rates.

2. **Fuzz Test: Malformed Config TOML** — Feed 10K randomly-generated TOML snippets; must never crash, must always return a parse error or valid config. Extends `corrupt_config_recovery_tests.cpp`.

3. **Fuzz Test: RPC Message Boundary Corruption** — Inject bit flips and truncations into the msgpack stream before decoding. Should handle gracefully or close connection. Extends `mpack_fuzz_tests.cpp`.

4. **Unicode Property Coverage** — Generate 100K random UTF-8 byte sequences; validate against reference implementation, check cell width, check combining character detection. Extends `celltext_unicode_corpus_tests.cpp`.

5. **Grid Mutation Stress Test** — Allocate a 200×100 grid, perform 10K random cell writes (random position, text, colors). Assert no memory leak, no segfault, no corruption on out-of-order updates. No equivalent currently exists.

6. **Multi-Pane Lifecycle Torture Test** — Create 8 panes, split/close in random order, resize each, verify tree structure, verify focus tracking. Check for dangling pointers or dropped updates.

7. **Font Atlas Exhaustion Test** — Render a sweep of all assigned Unicode codepoints to trigger multiple atlas reallocations. Verify previously-rasterized glyphs still render correctly after reallocation.

8. **RPC Timeout + Recovery Test** — Simulate a 6-second RPC stall (exceeds `kRpcRequestTimeout` = 5s). Verify requests time out cleanly, subsequent requests succeed, no orphaned message IDs remain.

9. **Command Palette Keyboard Navigation** — Test that arrow keys, Enter, Escape, and search filtering all work correctly in the command palette. Currently there are no unit tests for `command_palette_host.cpp`.

10. **Keybinding Dispatch Edge Cases** — Test chord timeout (press prefix key, wait, press key while app is busy), rapid modifier changes, and text input suppression. Currently `input_dispatcher_routing_tests.cpp` has only ~20 test cases covering the happy path.

---

## WORST 10 EXISTING FEATURES (Fragile / Poorly Implemented / Problematic)

1. **MegaCity Initialization** — All-or-nothing init; if TreeSitter grammar is missing, the entire feature silently fails. No degraded mode (e.g., city without semantic coloring). Generic error strings make diagnosis difficult.

2. **VT Mouse Reporting** — `mouse_reporter.cpp` (151 lines) handles basic click/drag but not: rapid motion at high DPI, button state edge cases, or interaction with scrollback offset. Known scroll-wheel granularity difference from xterm; no tests cover these paths.

3. **Neovim UI Event Parsing** — `ui_events.cpp` decodes `grid_line`, `hl_attr_define`, `mode_info_set` with no error handling for out-of-order events or missing initialization. Undefined behavior if Neovim changes event ordering.

4. **Scrollback Ring Buffer Offset Tracking** — Buffer wraps but offset arithmetic is complex. Tests exist but there is no exhaustive validation that all historical positions map correctly. Suspected off-by-one risks at wrap boundary.

5. **Font Style Detection by Filename Pattern** — Checking for `*Bold*`/`*Italic*` in filenames is fragile and breaks for fonts using non-English weight names or numerical weight suffixes. No fallback to OpenType font metadata.

6. **Keybinding Chord Timeout** — There is no timeout on the prefix-active state. A user who presses the chord prefix, is interrupted, then presses a key 30 seconds later will still trigger the bound action. Surprising and unreliable UX.

7. **LCOV Coverage Import** — `lcov_coverage.cpp` reads a static `.lcov` file and maps coverage to city buildings. No validation that the coverage report matches the current codebase. Stale reports silently mislead the visualization.

8. **Grid Cell Cluster Truncation** — Clusters >32 bytes are truncated (`grid.h` TODO). Rare but possible with long combining sequences or CJK clusters. Truncation is silent — no warning, no visible indication.

9. **Selection Size Hard Cap** — `selection_manager.cpp` truncates selection at 8,192 cells with no documented reason and no user-visible warning. Silently loses data for large selections.

10. **Render Test Tolerance Hardcoding** — `render_test.cpp` uses hardcoded pixel diff tolerance and changed-pixel thresholds. Different GPUs, drivers, or OS font rendering stacks may produce slightly different pixels, causing intermittent test failures that are painful to diagnose and bless.
