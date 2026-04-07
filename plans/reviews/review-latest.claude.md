# Draxul Codebase Review Report

**Codebase size:** ~62,000 lines of C++/ObjC++ source, ~27,000 lines of tests, ~34 shader files (Metal + GLSL)

**Architecture:** A Neovim GUI frontend with terminal emulation, split panes, workspace tabs, and an optional 3D code-city visualization. Renders via Metal (macOS) / Vulkan (Windows). Communicates with `nvim --embed` over msgpack-RPC pipes.

---

## Top 10 GOOD Things About the Codebase

### 1. Exemplary Dependency Injection Architecture
The `AppDeps` / `Deps` pattern is used consistently throughout (`App::AppDeps`, `InputDispatcher::Deps`, `HostManager::Deps`, `GuiActionHandler::Deps`, `CommandPaletteHost::Deps`, `ChromeHost::Deps`). Every major subsystem receives its dependencies as a struct of interfaces/callbacks, making every component independently testable without mock frameworks. This is visible in `app/app.h` (lines 38–57) and echoed in every major class.

### 2. Clean Module Separation via Static Libraries
The project is decomposed into 18 focused static libraries (`draxul-types`, `draxul-grid`, `draxul-nvim`, `draxul-font`, `draxul-renderer`, `draxul-host`, `draxul-config`, `draxul-window`, `draxul-ui`, `draxul-gui`, `draxul-runtime-support`, etc.). Each has a public `include/draxul/` header and private `src/`. Dependency arrows flow downward: `app → host → grid → types`. This is well-structured CMake.

### 3. Robust RPC Layer with Defensive Threading
`NvimRpc` (`libs/draxul-nvim/src/rpc.cpp`) has bounded notification queues (4096 max, 512 warn), timed-out request tracking with bounded eviction, main-thread-call assertion via `set_main_thread_id()`, proper `read_failed_` propagation, and exception-safe dispatch. The reader thread gracefully handles pipe EOF, read errors, and malformed packets.

### 4. Comprehensive Test Infrastructure
26,560 lines of tests including: RPC integration tests with a real fake server binary (`draxul-rpc-fake`), crash recovery tests, nvim process lifecycle tests, visual regression render tests with BMP diff and blessing workflow, DPI hotplug tests, startup rollback tests, Unicode corpus tests against a real nvim oracle, and 14 fake/helper files in `tests/support/`.

### 5. Shared Shader Constants Across Metal and GLSL
`shaders/decoration_constants_shared.h` and `quad_offsets_shared.h` are cross-API `#define` headers consumed by both Metal and GLSL pipelines. This eliminates constant drift between rendering backends — a common source of platform-specific bugs.

### 6. Thread Safety by Design
The `MainThreadChecker` utility (`libs/draxul-types/include/draxul/thread_check.h`) is embedded as a member in `Grid`, `GridRenderingPipeline`, and `MetalRenderer`, asserting main-thread access in debug builds. The nvim clipboard uses a mutex-protected cache refreshed on the main thread to avoid calling SDL from the reader thread.

### 7. Clean Init/Shutdown with Rollback Guard
`App::initialize()` (`app/app.cpp`, lines 169–378) uses a `time_step` lambda for profiled init stages and an RAII `InitRollback` guard that calls `shutdown()` on any failure path. Each init step returns early on failure with a descriptive `last_init_error_`. Startup timing is recorded for the diagnostics panel.

### 8. Well-Designed Grid Abstraction
The `IGridHandle` / `IGridRenderer` split cleanly separates per-host grid state from shared renderer resources. Each host owns its own `IGridHandle` with independent GPU buffers and viewport, while the renderer manages only global resources (atlas, cell metrics, swapchain). This enables multi-pane rendering without renderer god-object bloat.

### 9. Toast Notification System with Early-Buffer Replay
The `push_toast()` callback buffers toasts that arrive before the toast host exists during init, then replays them after the host is created (`app.cpp`, lines 352–356). Config warnings, font warnings, and early failures are never silently dropped.

### 10. Known-Bug Tracking with Structured Work Items
The `plans/work-items/` directory contains 11 bug reports each with severity, provenance (which AI/human found it), investigation steps, and fix strategies. This shows a disciplined approach to technical debt management.

---

## Top 10 BAD Things / Code Smells / Concerns

### 1. `App` Class is Becoming a God Object
`app/app.h` has 50+ member variables and 40+ methods. `app.cpp` is over 1100 lines. The class owns the window, renderer, text service, config, diagnostics host, palette host, toast host, input dispatcher, chrome host, workspaces, render tree, and diagnostics collector. The `wire_gui_actions()` method alone is 200+ lines of lambda wiring (`app.cpp`, lines 553–764).

### 2. Massive Duplicated Lambdas in `wire_gui_actions()`
`app/app.cpp`, lines 553–764: `GuiActionHandler::Deps` has 25+ `std::function` callback fields, all wired as inline lambdas capturing `this`. The `on_split_vertical` and `on_split_horizontal` lambdas are nearly identical — textbook code duplication. Adding a new action requires 4-step changes across separate files with no compile-time enforcement.

### 3. RPC Reader Thread Can Stall Forever on Invalid msgpack (Known Bug `00`)
`libs/draxul-nvim/src/rpc.cpp`, line 375: When `decode_mpack_value()` returns `consumed == 0` on an undecodable prefix byte, the inner `while` loop breaks without advancing `read_pos`. On the next outer read, additional bytes append to `accum` but `read_pos` still points at the bad byte — an infinite stall.

### 4. Terminal Drain Loop Has No Budget Cap (Known Bug `02`)
`libs/draxul-host/src/terminal_host_base.cpp`, lines 56–72: The `do { drain; process; drain; } while (!empty)` loop has no iteration or byte budget. A fast-producing process (e.g. `cat /dev/urandom`) can starve the render loop indefinitely, freezing the UI.

### 5. Cell Struct Layout Duplicated Across Shaders
The `Cell` struct is defined independently in three places: `grid.metal`, `grid_bg.vert`, and `grid_fg.vert`. Adding or reordering a field in one place but not the others will cause the GPU to silently read garbage. The shared-header approach used for decoration constants is not applied here.

### 6. `std::clamp` UB When `grid_rows() == 0` (Known Bug `01`)
`libs/draxul-host/src/terminal_host_base_csi.cpp`: `csi_margins()` calls `std::clamp(x, 0, grid_rows() - 1)` where `grid_rows() == 0` makes the upper bound -1. Per the C++ standard, `std::clamp(x, lo, hi)` with `lo > hi` is undefined behavior.

### 7. `SplitTree` Uses O(n) Recursive Lookup with `std::function` Allocations
`app/split_tree.cpp`: Every `find_leaf_node()`, `find_parent_of()`, and `find_divider_node()` call allocates a `std::function<>` and recursively walks the entire tree. Several operations chain multiple find calls making common paths O(n²). The `std::function` closure allocations are unnecessary given the small tree size.

### 8. Dead Linux Code Paths Silently Bitrotting
`main.cpp` (lines 109–127) and `NvimProcess` have `#ifdef __linux__` paths, but there is no Linux CMake preset, no Linux CI, and no Metal/Vulkan renderer for Linux. These dead code paths will diverge from the rest of the codebase silently.

### 9. MetalRenderer Dual-Personality ObjC/C++ Header is a Layout Hazard
`libs/draxul-renderer/src/metal/metal_renderer.h`, lines 86–112: `#ifdef __OBJC__` sections declare Metal objects as `ObjCRef<id<MTLDevice>>` in ObjC++ and `void*` in plain C++. If a new Metal handle is added to the ObjC section but not the C++ fallback (or vice versa), struct layout silently differs between translation units, causing memory corruption.

### 10. Critical Path Test Coverage Gaps
No tests for: Metal or Vulkan renderer, font shaping/ligature resolution, `GridRenderingPipeline` flush-to-GPU path, selection manager drag semantics, alt-screen save/restore fidelity, workspace tab switching state preservation, or the toast notification lifecycle. The render tests cover visual output only for nvim-host scenarios.

---

## Best 10 Features to Add (Quality of Life Improvements)

### 1. Search/Find in Terminal Scrollback
Terminal hosts have a 2000-line scrollback buffer but no search. Add `Ctrl+Shift+F` to open a search overlay that highlights matches in scrollback and lets the user jump between them. This is the single most-requested terminal feature.

### 2. Session Persistence (Save/Restore Layout)
Persist the workspace/tab/split layout and host configurations to a file so that closing and reopening Draxul restores exactly the previous session. Track per-pane working directory, host kind, and split ratios.

### 3. Clickable URLs with OSC 8 Hyperlink Support
Detect URLs in terminal output and make them clickable (Ctrl+click to open in browser). Standard in modern terminals (iTerm2, WezTerm, kitty). Work item `20` already exists for this.

### 4. Shell Integration via OSC 133 Prompt Marks
Detect shell prompt boundaries via OSC 133, enabling "jump to previous/next prompt" and command-aware selection (double-click selects command output). Work item `24` exists for this.

### 5. Configurable Color Schemes / Theme Support
Currently only `[terminal] fg/bg` are configurable. Add full 16-color ANSI palette configuration plus named themes (Solarized, Dracula, Catppuccin) selectable from config or command palette.

### 6. Multi-Cell Ligature Rendering
Work item `80` exists. Properly support ligatures spanning more than 2 cells (`!==`, `>>>`, `<<=`) using HarfBuzz cluster boundaries.

### 7. Tab Name Editing
Workspace tabs have a `name` field in `app/workspace.h` but no UI to edit it. Allow double-clicking a tab to rename it; use OSC 7 working directory as a default name.

### 8. Pane Status Bar
Work item `78` exists. Show a thin status line at the bottom of each pane with host name, working directory, and grid dimensions — like tmux's per-pane status.

### 9. Config Error Line Numbers
Work item `79` exists. When `config.toml` has errors, report the exact line number and key name instead of generic warnings.

### 10. GPU-Accelerated Smooth Scrolling with Inertia
Extend the existing `scroll_offset_px` infrastructure with proper physics-based inertia and deceleration curves, similar to WezTerm/Alacritty's smooth scroll.

---

## Best 10 Tests to Add for Stability

### 1. Terminal Drain Budget Exhaustion Test
Verify `TerminalHostBase::pump()` does not block indefinitely when the child produces output faster than it can be consumed. Use a test host producing unlimited output and assert `pump()` returns within a bounded time budget.

### 2. `csi_margins` with Zero-Row Grid (UB regression)
Create a terminal host fixture, resize the grid to 0 rows, feed `CSI r`, and verify no crash, no assertion, and no UB (run under AddressSanitizer + UBSan).

### 3. RPC Invalid msgpack Recovery
Feed a stream with a bad byte (`0xC1`) followed by a valid response. Verify the reader delivers the valid response or cleanly reports failure — never stalls.

### 4. SplitTree Stress: Rapid Open/Close Cycles
Run 20+ rapid split/close/split cycles. Verify leaf IDs remain unique, focus tracking is correct, no descriptors have negative dimensions, and `leaf_count()` matches expected values.

### 5. Font Atlas Overflow and Reset
Feed enough unique glyphs (e.g., all CJK codepoints) to exhaust the 2048×2048 atlas. Verify `consume_atlas_reset()` returns true, the renderer gets a full re-upload signal, and no glyph renders as garbage.

### 6. Workspace Tab Switching Focus Preservation
Create two workspaces with different focused panes. Switch between them via `next_workspace()` / `prev_workspace()`. Verify focus is correctly restored in each workspace.

### 7. Alt-Screen Round-Trip Fidelity
Feed specific grid content, switch to alt screen (`ESC [?1049h`), modify it, switch back (`ESC [?1049l`), and verify cell-by-cell equality with the original main screen content.

### 8. Selection Manager Boundary Conditions
Test `SelectionManager` with: selection at (0,0), at the last cell, spanning a double-width character, at the `selection_max_cells` limit, and during a concurrent scroll event.

### 9. DPI Change Mid-Render with Multiple Panes
Extend `dpi_hotplug_integration_tests.cpp` to cover: DPI change with two open panes, DPI change while a toast is visible, and DPI change immediately after a workspace switch.

### 10. Concurrent RPC Notification Flood
Spawn the fake server in a mode that sends 1000+ notifications rapidly. Verify all are eventually drained, none are lost, and no deadlock occurs.

---

## Worst 10 Existing Features (Poorly Implemented, Fragile, or Problematic)

### 1. VT Parser State Machine is Incomplete
`libs/draxul-host/include/draxul/vt_parser.h` only handles Ground, Escape, CSI, and OSC states. DCS (sixel graphics, tmux status, `DECRQSS`), PM, APC, and SOS sequences fall through to `on_esc()`, producing garbled output for any application using them.

### 2. Shutdown Blocks Main Thread for 500ms (Known Bug `07`)
`NvimProcess::shutdown()` on POSIX sends SIGTERM then unconditionally sleeps 500ms before SIGKILL. Windows `ConPtyProcess::cleanup()` has a similar blocking wait. The app visibly freezes when closing panes with slow-to-exit processes.

### 3. SGR Mouse Button Code Wrong on Release (Known Bug `03`)
`libs/draxul-host/src/mouse_reporter.cpp`: On mouse release, `button_code` is set to 3 (X10 release code) even in SGR mode, where the actual button number should be preserved and release signaled by the `m` suffix. This breaks mouse reporting in vim, tmux, and any application using SGR mouse mode.

### 4. CellText Silently Truncates Long Grapheme Clusters
`libs/draxul-grid/src/grid.cpp`, lines 232–243: `CellText::assign()` truncates any cluster longer than 32 bytes with only a WARN log. Family emoji and flag sequences easily exceed 32 bytes, causing visible rendering corruption. The `// TODO: consider std::string` comment acknowledges the issue.

### 5. ConPTY Handle Double-Close Risk (Known Bug `05`)
`libs/draxul-host/src/conpty_process.cpp`: After closing pipe handles pre-`CreateProcess`, local variables are not set to `INVALID_HANDLE_VALUE`. If `CreateProcess` fails, the cleanup path may double-close the same handles, causing crashes on Windows.

### 6. VtParser Discards Partial UTF-8 at Buffer Boundary (Known Bug `04`)
`libs/draxul-host/src/vt_parser.cpp`, lines 90–98: When the plain text buffer exceeds 64KB, `flush_plain_text()` is called and `plain_text_.clear()` discards any partial UTF-8 sequence split across the boundary, corrupting the next character.

### 7. TreeSitter Scan Complete Flag Set on Partial Scan (Known Bug `06`)
`libs/draxul-treesitter/src/treesitter.cpp`: When the scan thread exits early due to `stop_flag_` or filesystem error, `scan_complete_` is still set to true. Consumers see a "complete" scan that is actually partial.

### 8. Mpack String OOM Vulnerability (Known Bug `09`)
The mpack decoder does not cap string/binary lengths before allocating. A corrupted msgpack stream with a huge length prefix (e.g., 4 GB string) causes an unbounded `std::string` allocation, crashing the process. Reachable via fuzz surface.

### 9. `wire_gui_actions()` is a Fragile Monolithic Wiring Method
`app/app.cpp`, lines 553–764: All 25+ GUI action callbacks are wired in a single 200-line method. Adding one action requires changes in four separate locations with no compile-time enforcement. The split/horizontal and split/vertical handlers are nearly identical.

### 10. SplitTree Descriptor OOB Placement on Small Windows (Known Bug `08`)
`app/split_tree.cpp`, lines 349–358: When the window is very small, `first_w + div_w + second_w` can exceed available width because both children are clamped to `min(1)` independently. The second child's origin can exceed the parent's bounds, placing the pane partially off-screen.
