Here is the full review report:

---

# Draxul — Comprehensive Code Review

> **Scope**: All source files under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, `plans/`, and `docs/`. Active work items, completed items, and iceboxed items were cross-checked against this report to avoid duplication.

---

## 1. Architecture & Module Layout

Draxul is a modern C++20 Neovim GUI frontend targeting Metal (macOS) and Vulkan (Windows). It is built from 15 discrete CMake libraries with a clean acyclic dependency graph. The top-level orchestration lives in `app/`, with rendering, font, grid, host-emulation, and RPC concerns each in their own library. This is the primary architectural strength of the project.

### Module Graph (documented in `docs/module-map.md`)

```
draxul-types (header-only, no deps)
   ├── draxul-window
   ├── draxul-renderer
   ├── draxul-font
   └── draxul-grid
          └── draxul-nvim
                 └── draxul-host
                        └── draxul-app-support
                               └── app (executable)
```

This ordering is honoured throughout the build system. No upward linkages were observed.

---

## 2. Specific Code Smells & Maintainability Issues

### 2.1 Large multi-responsibility functions

| Function | File | Lines | Problem |
|---|---|---|---|
| `App::initialize()` | `app/app.cpp` | ~122 | Loads config, creates window, renderer, font, UI, host sequentially. Hard to unit-test the middle. |
| `App::run_render_test()` | `app/app.cpp` | ~110 | 7+ `std::optional<>` state variables, no named state enum, silent hang on missed transition. |
| `App::pump_once()` | `app/app.cpp` | ~57 | Mixes dead-pane cleanup, host pumping, window-event dispatch, and frame submission in one loop. |
| `Grid::scroll()` | `libs/draxul-grid/src/grid.cpp` | ~126 | Four sibling if-branches for ±row/±col, each with nested loops; a `scroll_rows()`/`scroll_cols()` split would help. |
| `TerminalHostBase` CSI dispatch | `libs/draxul-host/src/terminal_host_base_csi.cpp` | large | Large switch-per-final-byte; extensible but every new sequence adds to an already-long function. |

### 2.2 Error handling inconsistency

Three incompatible error-reporting idioms coexist:

- `bool + error_ member` — used by `HostManager`, `App::initialize`
- `std::optional<Error>` — used in parts of the RPC layer
- Silent early-return with warning log — used by `Grid::set_cell`, `Grid::scroll` on invalid bounds

The inconsistency makes call-site error handling unpredictable. A codebase-wide decision (e.g. "all public APIs return `expected<T, Error>`") would help.

### 2.3 Unsafe or unchecked operations

1. **Grid index overflow** — `row * cols_` in `grid.cpp` is not overflow-checked before the cast. For pathological terminal sizes this is silent UB.
2. **`void*` command-buffer pointers** — `IRenderContext::native_command_buffer()` and `native_render_encoder()` in `base_renderer.h` return `void*`. Comments explain the cast, but this bypasses the type system entirely. A `std::variant<id<MTLCommandEncoder>, VkCommandBuffer*>` or a template CRTP approach would be safer.
3. **`std::getenv("APPDATA")` empty string** — `app_config_io.cpp` null-checks but not empty-checks the env var; an empty APPDATA would produce a config path of just the filename.
4. **`FT_Face` lifetime in `glyph_cache.cpp`** — the cache stores a raw `FT_Face` pointer from `TextService`; if `TextService` is reinitialized (font size change) and the old face freed, accessing the cached pointer is UB. No lifetime guard is present.
5. **Signed-to-int truncation in `ui_events.cpp`** — `(int)value.as_int()` silently wraps if Neovim sends a very large attribute ID.

### 2.4 Duplicated logic with no shared abstraction

| Pattern | Locations |
|---|---|
| "get or create highlight attr ID" | `terminal_host_base.cpp:attr_id()` and `ui_events.cpp:handle_grid_line()` — both walk a map, compact on threshold |
| Font metrics recompute + apply | `App::initialize_text_service()`, `App::on_display_scale_changed()`, `App::apply_font_metrics()` — three call sites for the same cascade |
| Physical-pixel scaling | `InputDispatcher::to_physical()` (private helper) vs. inline lambda in `App::wire_window_callbacks` |

### 2.5 `void*` render-pass context

`IRenderPass::record(IRenderContext&)` is a clean abstraction, but `IRenderContext` exposes `void*` handles that every pass author must bridge-cast correctly. This is the single weakest point in the otherwise excellent renderer hierarchy. A templated or platform-tagged variant would eliminate the footgun.

### 2.6 State machine in `run_render_test`

The render-test path uses at least 7 independent `std::optional<>` flags (`diagnostics_enabled`, `ready_since`, `quiet_since`, `capture_requested`, …) without a named state enum. A missed transition silently times out. This is a high-value refactor target and a testing blind spot.

### 2.7 `CellText::kMaxLen = 32` truncation

`grid.h` has a `TODO: consider std::string for >32-byte clusters`. A ZWJ sequence with many combining marks can exceed 32 bytes. The truncation is silent (cell is updated, excess bytes dropped). It should at minimum emit a `WARN`, and ideally be a small-string optimisation that upgrades to heap.

### 2.8 Scrollback capacity is compile-time

`kCapacity = 2000` in `scrollback_buffer.h` is a `static constexpr`. Making it runtime-configurable (from config) requires only a constructor argument, but currently nothing threads that through. The icebox item `configurable-scrollback-capacity` has noted this.

### 2.9 `HostManager` uses `dynamic_cast` for `I3DHost`

`host_manager.cpp` does a one-shot `dynamic_cast` at startup to detect 3D hosts and call `attach_3d_renderer()`. The icebox item `hostmanager-dynamic-cast-removal` already flags this. While it works, it makes the type hierarchy harder to extend.

### 2.10 MegaCity is entangled with the core build

`DRAXUL_ENABLE_MEGACITY` is ON by default. The demo host pulls in SQLite and tree-sitter, non-trivial dependencies for users who only want a Neovim frontend. The icebox item `megacity-removal-refactor` acknowledges this. Until it is actioned, first-time build times are unnecessarily long.

---

## 3. Testing Holes

| Area | Gap |
|---|---|
| `App::initialize()` rollback | `InitRollback` RAII guard is not exercised by any test; a partial init failure (e.g. renderer success, font failure) is untested. |
| `App::run_render_test()` state machine | The 7-state render-test loop is never exercised in unit tests; bugs here surface only in CI render-test runs. |
| `InputDispatcher` chord prefix state | `prefix_active_` / `suppress_next_text_input_` transitions are not directly tested; icebox item `chord-prefix-stuck` already notes a known bug here. |
| `Grid` out-of-bounds access | No test explicitly sends invalid row/col to `set_cell()` or `scroll()` to verify safe handling. |
| Config parsing edge cases | No test for missing config file fallback, empty APPDATA, or malformed TOML (library error propagation). |
| `ScrollbackBuffer` ring-wrap | Ring buffer wrap-around at exactly `kCapacity` rows is not tested; column mismatch on restore is tested only implicitly. |
| `SdlEventTranslator` unit tests | Icebox item `sdl-event-translator-unit-tests` already calls this out. |
| CSI cursor boundary | Icebox item `csi-cursor-boundary` calls this out. |
| RPC pipe fragmentation | Icebox item `rpc-fragmentation-pipe-boundary` calls this out. |
| Metal headless initialisation | Icebox item `metal-headless-init` calls this out. There is no test that exercises the Metal renderer init path without a real display. |

---

## 4. Separation-of-Concerns Opportunities

1. **`TextServiceLifecycle`** — font metrics computation is scattered across three `App` methods. A small coordinator object would own the cascade and be testable in isolation.
2. **`AttributeCache`** — the "get or create highlight attr, compact on threshold" pattern appears in both the terminal and Neovim paths. A shared cache class would remove the duplication.
3. **`RenderTestStateMachine`** — the `run_render_test` optional-flag soup deserves an explicit state enum and a small state-machine class, making it testable and readable.
4. **`PixelScaleHelper`** — physical/logical pixel conversion is reinvented in two places; a one-line utility struct would remove the duplication.
5. **`GridScrollOps`** — `Grid::scroll()` four-branch implementation could be two free functions (`scroll_rows`, `scroll_cols`) called by a thin dispatcher, improving readability and testability.

---

## 5. Top 10 Good Things

1. **Clean acyclic library graph** — 15 libraries with no upward dependencies, documented in `docs/module-map.md` and enforced by the build system. This is rare and valuable.
2. **Render snapshot testing** — TOML scenario files, per-platform BMP references, and a `bless` workflow give high-confidence regression detection for visual output.
3. **Two-pass instanced GPU rendering** — background and foreground drawn in minimal draw calls; glyph atlas is shelf-packed and incrementally uploaded; 112-byte cell layout is compact and coherent.
4. **Comprehensive CI** — six GitHub Actions workflows cover build, ASan/UBSan, LLVM coverage, clang-format lint, SonarCloud, and docs generation on both Windows and macOS.
5. **C++20 throughout** — structured bindings, `std::optional`, `std::span`, `string_view`, ranges, and concepts are used consistently. No legacy C cruft.
6. **`IRenderPass` / `IRenderContext` abstraction** — external subsystems (MegaCity cube, future overlays) register render passes without touching renderer internals. Clean extension point.
7. **Thread-check assertions** — `DRAXUL_ASSERT_THREAD` macros catch thread-affinity bugs at the point of violation, not as data corruption downstream.
8. **Work-item discipline** — 100+ completed items tracked in `plans/work-items-complete/`, 37 iceboxed with rationale, 1 active. The project history is unusually legible.
9. **`replay_fixture.h`** — lets VT-parsing and grid-update tests work without spawning Neovim. The redraw-event builder API is clean and reusable.
10. **Keybinding chord system** — tmux-style `"ctrl+s, |"` syntax with conflict detection, full roundtrip config persistence, and a dedicated parser that is independently testable.

---

## 6. Top 10 Bad Things

1. **`void*` render context handles** — `IRenderContext::native_command_buffer()` / `native_render_encoder()` bypass the type system. Every render pass author must know the platform and cast correctly. A typed variant or platform-tagged accessor would eliminate this footgun.
2. **`run_render_test()` state machine** — 7+ uncoordinated `std::optional<>` flags with no named state enum and no unit tests. A missed transition causes a silent timeout in CI.
3. **Three incompatible error-reporting idioms** — `bool + error_ member`, `std::optional<Error>`, and silent early-return with a log message are all used. Call-site handling is inconsistent.
4. **`FT_Face` lifetime hazard** — `glyph_cache.cpp` stores a raw `FT_Face` pointer from `TextService`. If the face is freed (font size change reinit), subsequent cache use is UB with no guard.
5. **Chord prefix state machine untested, with known stuck-state bug** — the icebox item `chord-prefix-stuck` documents a known bug in prefix handling, yet there are no direct unit tests for the chord state machine.
6. **`CellText` 32-byte silent truncation** — combining-heavy ZWJ sequences can exceed the limit; truncation is silent with no warning log. The `TODO` in `grid.h` has been there long enough to deserve promotion to a work item.
7. **`dynamic_cast` for I3DHost detection** — one-shot runtime downcast in `HostManager` to discover 3D hosts. Fragile as the host hierarchy grows; icebox item exists but no plan to action it.
8. **MegaCity ON by default** — pulls in SQLite and tree-sitter unconditionally, increasing first-build times and binary size for users who only need the Neovim frontend.
9. **`App::initialize()` not unit-tested** — the `InitRollback` RAII guard is the safety net for partial-init failures, but it is never exercised by a test. A renderer-succeeds/font-fails scenario could silently leave resources in an invalid state.
10. **Grid index arithmetic not overflow-checked** — `row * cols_` in `grid.cpp` is cast after multiplication. For large terminal dimensions this is undefined behaviour with no diagnostic.

---

## 7. Best 10 Quality-of-Life Features to Add

These are ranked by breadth of daily-use impact and are not present in `docs/features.md` or active/completed work items.

1. **Live config reload** — watch `config.toml` for changes and hot-apply font size, scroll speed, and keybindings without restart. (Icebox: `live-config-reload`.) This would remove the most common reason to restart the app.
2. **Per-pane font size** — allow independent font sizes per split pane via keybinding or config. Useful when comparing wide and narrow code side-by-side. (Icebox: `per-pane-font-size`.)
3. **Searchable scrollback** — `/`-triggered incremental search through the scrollback ring buffer with highlight and jump. (Icebox: `searchable-scrollback`.) Critical for shell workflows.
4. **URL detection and click-to-open** — parse hyperlinks in terminal output (OSC 8 or heuristic regex) and open with the system browser on click. (Icebox: `url-detection-click`.) A common expectation from every modern terminal.
5. **Session restore** — persist split-tree layout, host types, and working directories to disk, restored on next launch. (Icebox: `session-restore`.) Single biggest friction reducer for power users.
6. **Window title from Neovim** — forward `nvim_set_current_win` title and the active buffer filename to the OS window title bar. (Icebox: `window-title-from-neovim`.) Makes taskbar/Exposé switching useful.
7. **Command palette** — fuzzy-search overlay for all bound actions plus recently opened files. (Icebox: `command-palette`.) Discoverability win for new users.
8. **Per-monitor DPI font scaling** — automatically re-rasterize at the correct PPI when a window is dragged between displays of different density. (Icebox: `per-monitor-dpi-font-scaling`.) High-impact on mixed-DPI setups.
9. **Configurable ANSI palette** — allow the 16 base ANSI colours to be remapped in `config.toml` instead of relying solely on the Neovim/shell colour scheme. (Icebox: `configurable-ansi-palette`.) Required for theme portability.
10. **Performance HUD overlay** — extend the existing diagnostics panel with per-frame GPU time, atlas pressure graph, and dirty-cell rate over time. (Icebox: `performance-hud`.) Essential during font or theme changes that stress the glyph cache.

---

## 8. Best 10 Tests to Add for Stability

Items already in the icebox as test work items are included where they represent the highest-value gaps.

1. **`App::initialize()` partial-failure rollback** — exercise `InitRollback` by injecting a fake renderer that succeeds but a fake text-service that fails; assert no resource leak and a clean error message.
2. **`run_render_test()` state-machine transitions** — unit-test each state change (startup → quiet → capture → compare) using a fake frame clock and fake renderer, asserting the correct outcome string.
3. **Chord prefix stuck-state** — send a chord prefix key, then send a non-matching key, then verify normal input is unblocked and the prefix state is reset. (Icebox: `chord-prefix-stuck`.)
4. **Grid out-of-bounds writes** — call `set_cell()` and `scroll()` with every combination of boundary-violation (negative row, col ≥ cols, scroll delta > region) and assert no crash, no dirty-flag corruption.
5. **`ScrollbackBuffer` ring-wrap at capacity** — fill to exactly `kCapacity` rows, write one more, verify the oldest row is evicted correctly and viewport offset clamps.
6. **`SdlEventTranslator` key mapping** — table-driven tests covering function keys, modifier combos, and media keys. (Icebox: `sdl-event-translator-unit-tests`.)
7. **CSI cursor boundary clamping** — send cursor-move sequences that would place the cursor outside the grid and assert it is clamped to the last valid position, not wrapped or ignored. (Icebox: `csi-cursor-boundary`.)
8. **RPC pipe fragmentation** — split a valid msgpack-RPC message at every byte boundary and feed it to the codec in two reads; assert correct decode. (Icebox: `rpc-fragmentation-pipe-boundary`.)
9. **Config file not found** — delete `config.toml` and call `AppConfig::load()`; assert defaults are applied and no exception or crash occurs. Also test malformed TOML and an empty APPDATA env var.
10. **Attribute cache compaction** — fill highlight table to just above `kAttrCompactionThreshold`, trigger a full grid scan, and verify no attr IDs present in the live grid are evicted.

---

## 9. Worst 10 Existing Features

Ranked by impact on daily usability, correctness, and maintainability.

1. **`CellText` 32-byte hard truncation** — ZWJ emoji sequences beyond 32 bytes are silently dropped, corrupting cell content with no user-visible warning. This is a correctness defect masquerading as a limitation.
2. **Fixed 2000-row scrollback** — the limit is a compile-time constant. Heavy shell users hit it quickly and there is no way to adjust without rebuilding from source.
3. **Chord prefix stuck state** — a documented bug (icebox `chord-prefix-stuck`): after an unrecognised chord suffix, the prefix active flag is not always cleared, blocking subsequent input until focus change.
4. **Font variant resolution by filename** — bold and italic are found by appending `-Bold`, `-Italic`, etc. to the font filename. This breaks for any font that doesn't follow that naming convention, requiring the user to specify all four paths manually.
5. **`void*` render context** — `IRenderContext` exposes raw `void*` for the platform command buffer. A miscast (e.g. during a Vulkan/Metal port or test stub) produces a silent crash. The footgun is documented but not prevented.
6. **Render tests gated on a CMake flag** — `DRAXUL_ENABLE_RENDER_TESTS` must be explicitly set; it is ON by default but the CLI bless workflow requires knowing this. A first-time contributor who builds with a custom preset may have render tests silently absent.
7. **`open_file_dialog` unbound by default** — the feature exists, is wired up, and works, but ships with no default keybinding. Users who don't read the config reference never discover it.
8. **No window-title feedback from Neovim** — the OS window title is always the static app name. Context-switching between multiple Draxul windows (different repos, different files) requires guessing from the taskbar thumbnail.
9. **MegaCity is ON by default** — the 3D demo pulls in SQLite and tree-sitter, extending first-build time by tens of seconds and binary size noticeably, for functionality most users never access.
10. **No configurable ANSI palette** — the 16 base terminal colours are fixed. Shell colour themes that rely on the host terminal's ANSI palette rather than explicit 256-colour or RGB codes will render with wrong colours.

---

*Report generated 2026-03-26 against branch `main` at commit `03b5afb`.*
