Now I have everything I need. Here is the full review report:

---

# Draxul Codebase Review

## Overview

Draxul is a GPU-accelerated Neovim GUI frontend written in C++20, supporting Metal (macOS) and Vulkan (Windows). The codebase spans ~13 libraries, ~100 implementation files, and 61 test files totalling ~18,000 lines. This report covers architecture, code quality, stability, and prioritised recommendations.

---

## Top 10 Good Things

1. **Strict layered dependency graph.** The `draxul-types → window/renderer/font/grid → nvim → app-support → app` chain has no circular dependencies. CMake enforces this structurally. Multi-agent work can be done on any library without risking unexpected coupling.

2. **Three-tier renderer hierarchy.** `IBaseRenderer → I3DRenderer → IGridRenderer` is a clean, extensible design. Adding a new rendering capability (e.g., a post-process pass) only requires touching one level of the hierarchy. The `IRenderPass` / `IRenderContext` abstraction cleanly replaces legacy `void*` callbacks.

3. **Host abstraction hierarchy.** `IHost → I3DHost → IGridHost → GridHostBase → TerminalHostBase → LocalTerminalHost → {NvimHost, LocalTerminalHost subclasses}` is deep but well-motivated. Each layer adds exactly the right surface area. The Template Method pattern (`initialize_host()`, `do_process_*`) cleanly separates policy from mechanism.

4. **Comprehensive test suite.** 61 test files, ~18,000 lines, covering VT100 compliance, msgpack RPC, key encoding, grid mutation, dirty tracking, scrollback, rendering pipeline, config, split tree, and app lifecycle. `replay_fixture.h` allows reproducing Neovim redraw bugs without spawning a process.

5. **RAII throughout.** The `InitRollback` guard in `app.cpp`, smart pointer ownership chains, and consistent use of `std::unique_ptr` / `std::shared_ptr` mean no raw owning pointers in application code. The shutdown path is clean and deterministic.

6. **Return-based error propagation.** Hot paths use `bool` / `std::optional` / out-parameters rather than exceptions. This is consistent across all library interfaces and avoids unwinding overhead in the render loop.

7. **Platform separation.** Metal and Vulkan backends share only the abstract renderer interface. Process spawning (`CreateProcess` / `fork+exec`), font discovery, and clipboard are all behind platform abstractions. `#ifdef` guards are confined to dedicated files.

8. **CMake presets with sanitizer support.** The `mac-asan` preset enables AddressSanitizer + LeakSanitizer with a single flag. This is a low-friction path to catching memory errors early. Coverage flags are also wired for LLVM source-based coverage.

9. **UTF-8 safety.** `detail::utf8_next_codepoint` and `utf8_valid_prefix_length` in `grid.h` are a hand-rolled but correct and well-commented UTF-8 validator. All grid cell writes go through this, preventing truncation mid-codepoint.

10. **Developer automation.** `do.py` covers build, smoke test, render test blessing, and snapshot comparison. `gen_uml.py` / `gen_deps.py` generate diagrams from source. The `clang-format` pre-commit hook enforces formatting automatically. The planning/icebox/complete work item structure gives clear project state.

---

## Top 10 Bad Things

1. **Chord-prefix-stuck bug (icebox `01-chord-prefix-stuck`).** `InputDispatcher` enters prefix mode on a configurable prefix keydown and never resets on key-up without a matching chord. The next unrelated key is silently consumed. This is a live UX bug with a known fix documented but not implemented.

2. **`CellText` 32-byte truncation with data loss.** `CellText::assign()` (`grid.h:136-144`) truncates clusters silently beyond 32 bytes, emitting only a `WARN`. The `TODO` comment acknowledges this but it has not been addressed. Any sufficiently large combining sequence or emoji cluster is silently corrupted.

3. **`dynamic_cast<I3DHost*>` in `HostManager` for 3D renderer attachment (icebox `16`).** If a new host subclass does not inherit `I3DHost`, the cast silently returns null. No error, no log, no compile-time enforcement. This is an invisible correctness hazard as more host types are added.

4. **`UiEventHandler` uses raw pointer setters and public `std::function` callbacks.** `set_grid()`, `set_highlights()`, `set_options()` all store raw pointers with no lifetime guarantee. Callers must keep the pointed-to objects alive for the lifetime of `UiEventHandler`. There is no interface or assertion enforcing this. The `on_flush`, `on_grid_resize`, etc. members are public `std::function` fields — a callback interface object would be safer.

5. **Duplicate icebox entries.** Files `20 searchable-scrollback -feature.md`, `21 per-pane-env-overrides -feature.md`, and `22 bracketed-paste-confirmation -feature.md` each exist twice in `plans/work-items-icebox/` (with and without ` 1` suffix). This causes confusion about which copy is authoritative and whether either reflects current intent.

6. **`IHost` interface is too wide.** `IHost` has 14 pure virtual methods, including mouse move, mouse wheel, mouse button, key, text input, text editing, and more. Hosts that don't handle mouse (e.g., a headless render host) must still implement stubs for all of them. A smaller base interface with optional extension interfaces (like `IMouseHost`) would reduce boilerplate.

7. **Dead conditional blocks in `app.h`.** Lines 13-17 of `app.h` contain `#ifdef __APPLE__ / // Metal renderer header... #else / // Vulkan renderer header... #endif` comment blocks with no actual content. These are leftover scaffolding that adds noise and false signals to readers expecting platform-specific code.

8. **`MegaCityHost` (3D spinning cube demo) is production-compiled code.** The `draxul-megacity` library adds a full dependency on tree-sitter and non-trivial 3D rendering code that is never used in normal operation. Icebox item `17` calls for its removal but it remains. It consumes compile time, increases binary size, and creates maintenance burden.

9. **Agent review scripts not deduplicated (icebox `22`).** Multiple `ask_agent_*.py` scripts exist with duplicated argument parsing and output logic. This makes it difficult for multiple agents to collaborate on reviews using consistent tooling.

10. **`HostContext` uses references for required dependencies.** `HostContext` contains `IWindow&`, `IGridRenderer&`, `TextService&` as references. There is no null-check possible at compile time and a caller that passes a dangling reference produces undefined behaviour with no diagnostic. Using non-null pointers or a builder pattern with explicit validation would surface this class of error earlier.

---

## Best 10 Features to Add (Quality of Life)

1. **Live config reload (icebox `56`).** Allow changes to `config.toml` to take effect without restarting. Font size, keybindings, ligature toggle, scroll speed, and terminal colors could all be hot-reloaded. This is one of the most frequent causes of friction in day-to-day use and the config system already has the structure to support it.

2. **URL detection and click-to-open (icebox `20`).** Detect http/https URLs in rendered cells and open them with `xdg-open` / `open` on click or a keybinding. Most terminal emulators support this and its absence is conspicuous. The VT parser and cell model already have enough metadata to implement this without a regex post-pass.

3. **Command palette (icebox `60`).** A `Ctrl+Shift+P`-style overlay to invoke GUI actions, open splits, switch hosts, and run config changes interactively. With ImGui already integrated for the diagnostics panel, the UI layer is already present — a command palette is an incremental addition.

4. **IME composition visibility (icebox `29`).** CJK input via IME requires showing the composition string in-window. SDL3 already provides `SDL_TEXTEDITING` events and `on_text_editing` is wired on `NvimInput`. The missing piece is rendering the composition preedit inline.

5. **Native tab bar (icebox `57`).** A native or ImGui-rendered tab bar for managing multiple splits/hosts. The `SplitTree` already tracks multiple panes; a tab bar would be a presentation layer on top of the existing host manager.

6. **Configurable ANSI palette (icebox `33`).** Allow per-terminal ANSI color overrides in `config.toml`. Terminal users frequently need to tune the 16-color palette for readability with specific themes. The terminal color pipeline is already in place via `HighlightTable`.

7. **Window state persistence (icebox `36`).** Save and restore window size, position, split layout, and active host kind across sessions. This requires writing a small state file at shutdown and reading it at startup, and it dramatically reduces friction when reopening the app.

8. **Remote Neovim attach (icebox `30`).** Connect to an already-running `nvim --listen` instance over a socket rather than spawning a new process. This is essential for server workflows and makes Draxul viable as a drop-in replacement for SSH-forwarded Neovim sessions.

9. **Font fallback inspector (icebox `61`).** A debug panel showing which glyph came from which font face for a selected cell. Diagnosing missing glyphs or wrong-face selections is currently done by trial and error with log output. An in-app inspector would make font configuration tractable.

10. **Per-monitor DPI font scaling (icebox `19`).** On multi-monitor setups with mixed DPIs, moving the window between displays should trigger a font re-rasterisation at the new DPI. SDL3 provides the events; the font pipeline supports re-init; the missing piece is connecting the two when a display change event fires for a window move.

---

## Best 10 Tests to Add (Stability)

1. **Chord-prefix-stuck: key-up resets prefix mode.** Directly tests the known icebox `01` bug. Press prefix key, release without a chord, assert dispatcher is not in prefix mode and that the next key-down reaches the host.

2. **`CellText::assign()` with a 33-byte cluster does not corrupt adjacent cell data.** Verify truncation is safe: the truncated cell's `len` matches the valid prefix length, the adjacent cell's memory is unmodified, and no crash occurs under ASan.

3. **`UiEventHandler` with a null grid pointer does not crash.** Ensure that calling `process_redraw()` before `set_grid()` produces a graceful failure (early return or assertion) rather than a null-pointer dereference.

4. **`HostManager::split_focused()` during host initialization does not leave dangling state.** If the primary host fails `initialize()`, a subsequent `split_focused()` call should return `kInvalidLeaf` and not insert a host entry into the `hosts_` map.

5. **Atlas overflow reset does not drop dirty cells for a second host.** When the glyph atlas overflows and resets, cells belonging to a different `IGridHandle` should still be marked dirty and re-uploaded correctly, not silently skipped.

6. **VT scroll region clamped to grid bounds.** `CSI r` (DECSTBM) with `top >= bot` or `bot > rows` should clamp rather than scroll out-of-bounds. Current `grid_tests.cpp` covers normal scroll but not the invalid-region case.

7. **`TerminalHostBase::handle_osc()` with malformed OSC 7 URI does not crash.** A shell emitting `\e]7;not-a-url\a` should be silently ignored (or produce a WARN) rather than causing a parse error in the URL decoder.

8. **Font metrics change during active rendering does not leave renderer in inconsistent state.** Simulate a DPI-change event mid-frame: call `apply_font_metrics()` while a frame is notionally in progress and verify the renderer grid handle's buffer dimensions match the new cell size.

9. **`shutdown_race`: reader thread drain after nvim process exit.** The reader thread may still hold queued notifications when `NvimProcess::shutdown()` is called. Verify that draining the queue after the process has exited completes within a bounded timeout and does not block indefinitely.

10. **`SplitTree::close_leaf()` with a single leaf returns false without freeing the root node.** This edge case (last pane close) is documented in `HostManager` but a dedicated unit test for the underlying `SplitTree` invariant (`leaf_count() == 1` survives a `close_leaf()` attempt) is missing.

---

## Worst 10 Features

1. **Chord-prefix-stuck input state.** A GUI-level keybinding prefix that gets permanently stuck on accidental activation. The dispatcher swallows the next keystroke silently. This is a live usability regression that affects anyone who triggers the prefix inadvertently.

2. **`MegaCityHost` spinning cube.** A 3D demo host that adds a tree-sitter dependency, a full 3D render pass registration, and ImGui lifecycle wiring for a cube that spins in a window. It is behind a feature flag but is still compiled, linked, and tested. The icebox item to remove it has been open with no progress.

3. **`CellText` 32-byte hard cap with silent truncation.** Large emoji clusters (e.g., family emoji with skin tone modifiers) exceed 32 bytes. The cell silently renders a truncated glyph with no visible error. The `TODO` has been present since the data structure was introduced.

4. **Bold/italic font resolution by filename substitution.** Finding the bold or italic variant of a font by appending `-Bold`, `-Italic`, etc. to the filename is fragile. It fails for fonts with non-standard naming conventions and for system fonts installed without filename predictability. The icebox item for platform font API integration has not been scheduled.

5. **`UiEventHandler` public `std::function` callback fields.** Exposing `on_flush`, `on_grid_resize`, `on_cursor_goto`, etc. as public data members means any code can overwrite them at any time. The callback interface is set piecemeal across `NvimHost::initialize()`, making it easy to forget to wire one up and get a no-op silently.

6. **`dynamic_cast<I3DHost*>` silent failure in `HostManager`.** Adding a new host subclass that should participate in 3D rendering but forgets `I3DHost` produces no error at compile time, no warning at runtime, and no visible defect until a 3D pass is unregistered. The fix (virtual `attach_3d_renderer()` with default no-op on `IHost`) is documented in the icebox but unimplemented.

7. **Glyph atlas overflow causes full reset and full repaint.** When the atlas runs out of shelf space, it clears completely and all glyphs must be re-rasterised and re-uploaded. For a large terminal with many unique glyphs, this causes a visible frame stutter. A partial eviction strategy (LRU) would be more graceful.

8. **Agent review script duplication.** `scripts/ask_agent_claude.py`, `ask_agent_gpt.py`, `ask_agent_gemini.py` share boilerplate argument parsing, file reading, and output formatting with no shared base. Icebox item `22` tracks deduplication but it creates ongoing friction whenever review tooling needs to be updated.

9. **`IHost` requires 14 pure virtual stubs for non-interactive hosts.** A headless render test host, a replay-only host, or any future specialised host must provide no-op implementations of `on_mouse_move`, `on_mouse_wheel`, `on_text_editing`, `on_text_input`, and more. This creates a maintenance tax and increases the chance of a stub silently masking a real event dispatch.

10. **Duplicate icebox work items.** Three icebox work items (`20 searchable-scrollback`, `21 per-pane-env-overrides`, `22 bracketed-paste-confirmation`) exist in two copies each, with and without a ` 1` suffix. This confuses automated tooling, the do.py script references, and manual review alike. The duplicates carry risk of divergence and should be cleaned up.

---

*Report generated from live source file inspection across `app/`, `libs/`, `tests/`, `shaders/`, `plans/`, and `scripts/` as of 2026-03-23.*
