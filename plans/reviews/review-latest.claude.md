# Draxul Codebase Review — April 2026

---

## Methodology

All source files under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/` were read directly from disk. Completed and iced work items were excluded from recommendations. `docs/features.md` was consulted to avoid proposing already-implemented work.

---

## Architecture Overview

Draxul is a Neovim GUI frontend with a well-structured multi-library layout. The dependency graph flows cleanly downward: `draxul-types` (header-only) → window / renderer / font / grid → nvim/host → app-support → app (executable). Platform-specific GPU backends (Metal on macOS, Vulkan on Windows) are isolated behind renderer interfaces. The host abstraction supports terminal, Neovim, 3D, toast, chrome, and command-palette hosts within a pane-split workspace model.

---

## Top 10 Good Things

1. **Clean library dependency graph.** The downward-only library hierarchy (`types → window/renderer/font/grid → nvim/host → app`) prevents circular dependencies and makes each layer independently testable. The `CMakeLists.txt` files enforce this structurally.

2. **Strong interface abstractions.** `IHost`, `IWindow`, `IGridRenderer`, `IRenderPass`, and `IRenderContext` decouple implementations from consumers. Switching from Metal to a hypothetical WebGPU backend would touch only the renderer implementation.

3. **Render test harness.** TOML scenario files, reference image comparison, and a bless workflow (`py do.py blessbasic` etc.) form a real visual regression system — uncommon in terminal emulator projects of this size.

4. **Smoke-test integration.** `--smoke-test` flag with a 3-second startup timeout, exercised in CI via `do.py smoke`, catches broken includes, link errors, and init failures before code review.

5. **Logging infrastructure.** `DRAXUL_LOG_DEBUG/INFO/WARN/ERROR` macros with runtime-configurable categories and dual stderr + file output. CLI flags `--log-file` and `--log-level` are documented and reliable across platforms, including `.app` bundles.

6. **AppDeps / factory pattern.** `AppDeps`, `AppFactory`, and per-component `Deps` structs enable real dependency injection and make the test harness genuinely useful — production and test paths share the same component wiring.

7. **Host polymorphism hierarchy.** `IHost → I3DHost → IGridHost → GridHostBase` lets terminal/Neovim/3D hosts coexist cleanly without the base class needing to know about each variant. `HostManager` can manage all of them through a single `IHost*`.

8. **Thread-safe RPC queue.** Reader thread pushes decoded MPack events to a thread-safe queue; main thread drains per frame. The design keeps grid mutation and GPU work single-threaded and avoids complex locking in the hot path.

9. **Modular font pipeline.** FreeType → HarfBuzz → GlyphCache → shelf-packed atlas is a well-layered pipeline. Ligature enable/disable is a single flag, and the pipeline composes with both renderer backends without platform-specific branches in font code.

10. **Build system quality.** All dependencies via CMake FetchContent, sanitizer/coverage helpers, conditional compilation for MegaCity and render tests, and a single `do.py` orchestrator that handles MSVC preset selection, Metal compilation, and test running.

---

## Top 10 Bad Things

1. **`dynamic_cast<MegaCityHost*>` in HostManager (host_manager.cpp:430, 466).** `HostManager` casts to a concrete type to call `attach_3d_renderer()`. This couples the orchestrator to a specific implementation, breaks the open/closed principle, and will silently do nothing if `MegaCityHost` is renamed or the cast fails. The fix — a virtual `attach_3d_renderer()` on `I3DHost` — is obvious and the plumbing already exists.

2. **Over-parameterized `Deps` structs.** `GuiActionHandler::Deps` has 18 function-pointer fields; `HostManager::Deps` has 11. Structs this large are configuration bags in disguise: hard to construct in tests, easy to leave fields null (causing silent no-ops), and they obscure what a component actually depends on. Domain-specific facades or narrower service interfaces would halve the cognitive load.

3. **`ToastHost::active_` accessed without locks.** `pending_mutex_` guards the incoming queue, but `active_` (the live list of displayed toasts) is read in `draw()` and written in `pump()` without synchronization. If toasts are enqueued from a non-main thread and the pump/draw cycle runs on the main thread, this is a latent data race.

4. **`ChromeHost` is both renderer and orchestrator.** It renders the tab bar and dividers *and* coordinates workspace/pane actions via callbacks into the app layer. These two responsibilities should be separated: ChromeHost as pure renderer, with workspace coordination extracted to a `WorkspaceController` or handled by `App` directly.

5. **String-based host action dispatch.** `GuiActionHandler` calls `host->dispatch_action("copy")` / `dispatch_action("paste")` requiring each host to parse string tokens. This is stringly-typed polymorphism — typos are silent, adding a new action requires editing every host, and there's no static check that a given host supports an action. Virtual methods or a typed `enum class HostAction` would be strictly better.

6. **Config reload race.** `reload_config` reads the TOML file from disk and mutates `config_` in-place (app/app.cpp). If a render frame is in progress when the user triggers a reload, configuration fields can be read mid-mutation. The reload should be posted to the main-thread event queue rather than executed synchronously on input.

7. **Keybinding matching logic duplicated.** `InputDispatcher` implements a chord state machine; `CommandPalette` has its own keybinding lookup. `gui_keybinding_matches()` exists as a utility but the state-machine logic is not shared. Changes to chord semantics (e.g., timeout, cancellation) must be made in two places.

8. **No timeout on `UiRequestWorker` condition variable.** A resize request waits indefinitely for Neovim to acknowledge. If the Neovim process hangs or exits uncleanly, the resize path blocks forever. A 2–5 second timeout with fallback would make this resilient.

9. **Magic layout constants scattered across files.** `kDividerWidth = 4`, `tab_bar_height = ch + 2`, padding values in `split_tree.cpp`, `chrome_host.cpp`, and Metal/Vulkan shader uniforms are disconnected. Font-size changes require coordinating these independently. A `LayoutMetrics` struct computed once from font metrics and propagated would unify this.

10. **No tests for `InputDispatcher`, `HostManager` lifecycle, or `ToastHost`.** The chord state machine, pane close/restart/swap operations, and toast queue are recently added features with meaningful complexity, yet they appear in none of the 15+ test files. The risk of regressions here is high relative to the testing investment required.

---

## Top 10 Quality-of-Life Features to Add

*(Verified against `docs/features.md` — none of these are already implemented.)*

1. **Per-pane working directory tracking.** Each `NvimHost` pane should track `nvim`'s CWD (via `DirChanged` autocmd → RPC notify) and display it in the tab bar. Enables `open terminal here` on a pane, and makes multi-project workflows navigable without reading the Neovim status line.

2. **Session save/restore.** Serialize the `SplitTree` layout, per-pane host type, and working directories to a file on graceful exit; restore on next launch. Users lose layouts on restart; this would be the single most-requested missing feature for a day-to-day workflow tool.

3. **Mouse-resizable pane dividers.** `SplitTree` already tracks `kDividerWidth` rectangles and hit-tests click events. Adding drag-to-resize requires delta tracking in `InputDispatcher` and propagation to `SplitTree::resize_ratio()` — the scaffolding is largely there.

4. **Configurable tab-bar position (top / bottom / hidden).** ChromeHost hardcodes top placement. Bottom placement is preferred by many terminal users and is a one-line layout change once `LayoutMetrics` is unified (see bad thing #9). Hidden mode (show on hover) would clean up the UI for full-screen workflows.

5. **URL detection and click-to-open.** Scan grid cells for `https?://` patterns on redraw; underline matched runs; on Cmd/Ctrl+click, call `open` / `xdg-open`. Neovim plugins already do this inside the buffer, but the GUI layer can catch URLs in non-buffer contexts (build output, logs).

6. **Font fallback chain in config.** `config.toml` currently supports one font family. Unicode symbols, CJK, and emoji require secondary font fallback. The font pipeline (FreeType → HarfBuzz) already does per-codepoint shaping; a `font_fallback = ["Noto Sans CJK", "Apple Color Emoji"]` config array would wire this up.

7. **System-clipboard image paste.** On macOS `NSPasteboard` and Windows `OpenClipboard`, images are available as raw pixel data. Exposing this via a configurable `on_image_paste` shell command (e.g. `imgcat`, `wl-copy`) would let users paste screenshots into terminal workflows (e.g. base64 encoding them into Neovim).

8. **Pane zoom keyboard shortcut + visual indicator.** A zoom mode (temporarily maximizing one pane) already appears in `HostManager` but the visual indicator that a pane is zoomed is minimal. Adding a subtle border highlight or tab-bar badge (`[Z]`) prevents the common confusion of "where did my other panes go?"

9. **Clickable file paths in terminal output.** Similar to URL detection: grep grid cells for patterns matching `/path/to/file:line:col`, parse on Cmd+click, and open in the focused Neovim pane at that location. This closes the tight loop between compiler output and code navigation.

10. **Config file watcher with live reload.** Instead of a manual `:DraxulReloadConfig` command, use `kqueue` (macOS) / `ReadDirectoryChangesW` (Windows) to watch `config.toml` and reload automatically. The reload path exists; it just needs a file watcher trigger and the race fix described in Bad #6.

---

## Top 10 Tests to Add

1. **`InputDispatcher` chord state machine unit test.** Feed synthetic `KeyEvent` sequences through `InputDispatcher` in isolation (no window, no Neovim). Verify: chord prefix activates, chord completes fires action, non-matching key cancels prefix and re-dispatches, timeout cancels. This is the highest-risk untested code path.

2. **`HostManager` pane lifecycle integration test.** Drive `split_pane`, `close_pane`, `restart_host`, and `swap_pane` through `HostManager` with mock hosts. Verify the `SplitTree` geometry, focus tracking, and zoom-state machine are consistent after each operation.

3. **`ToastHost` concurrency test.** Push toasts from N threads concurrently while a main-thread loop calls `pump()` + `draw()`. Verify no crash, no lost toasts, and correct expiry. This will surface the current `active_` data race.

4. **`AppConfig` reload race test.** Trigger `reload_config` from a background thread while `App::run()` reads configuration fields. Use TSan (already supported via `mac-asan` preset which enables TSan-like detection) to catch the unsynchronized mutation.

5. **`SplitTree` resize arithmetic property test.** Generate random split sequences (H/V splits at random ratios), then resize the root rectangle and verify: pixel coverage is complete (no gaps), no pane has zero area, sum of leaf areas equals root area. Use a fuzz/property-based approach with a fixed RNG seed.

6. **Glyph atlas overflow test.** Fill the atlas past capacity with synthetic glyphs and verify the shelf-packer either evicts cleanly or logs a warning — rather than silently corrupting atlas state or writing out of bounds.

7. **`UiRequestWorker` timeout / hung-Neovim test.** Submit a resize request to `UiRequestWorker`, never acknowledge it from the mock Neovim side, and verify the worker unblocks within a bounded time. This test will currently hang, surfacing the missing timeout (Bad #8) directly.

8. **`CommandPalette` fuzzy search edge cases.** Empty query returns all actions sorted by recency; query longer than any action returns empty; query with Unicode normalizes to ASCII equivalent; two actions with identical score are returned in stable order.

9. **Config unknown-key and type-mismatch error handling test.** Feed `config.toml` variants with unknown keys, wrong-typed values, and out-of-range `scroll_speed`. Verify: no crash, appropriate `WARN` log emitted, the valid fields are still applied, and defaults are used for invalid fields.

10. **Workspace tab add/remove/switch round-trip test.** Add three workspaces, switch between them, close the middle one, verify focus lands on an adjacent workspace, verify tab indices are renumbered, verify no dangling `HostManager` pointers remain. This covers the interaction between `App`, `ChromeHost`, and workspace ownership.

---

## Worst 10 Features

*(Assessed by implementation quality, maintenance burden, and risk relative to value delivered.)*

1. **String-based `dispatch_action` host protocol.** Every host parses `"copy"`, `"paste"`, `"toggle_diagnostics"` from raw strings. There is no registry, no static check, no compile-time guarantee a host supports an action. Typos silently do nothing. This is the worst API in the codebase and will only get harder to maintain as more actions are added.

2. **MegaCityHost `dynamic_cast` coupling.** The entire 3D host integration hangs on a runtime cast to a concrete type in `HostManager`. It works today only because there is exactly one 3D host. The moment a second 3D host type exists, or `MegaCityHost` is refactored into a base + variant, this silently stops working.

3. **ChromeHost NanoVG hard-coupling.** The tab bar and dividers are rendered exclusively via `INanoVGPass`. If the NanoVG dependency is removed or swapped, the entire chrome UI disappears. The chrome renderer should depend on an abstract `IImmediateRenderer` interface, not a specific library.

4. **GuiActionHandler 18-field `Deps` struct.** This struct is effectively a global-variable bag disguised as dependency injection. It's impossible to tell which fields are required vs optional, and constructing it in tests requires filling in 18 fields or using nullptr with the hope the code path doesn't hit them. It actively discourages writing tests.

5. **`reload_config` synchronous disk read on input event.** Reading and parsing TOML from disk synchronously in the event handler blocks the main thread and races with the render path. It's a simple operation today, but as config grows, this will introduce frame hitches and data races that are difficult to reproduce.

6. **Magic layout constants (divider width, padding) scattered across files.** `kDividerWidth`, tab bar height formulas, and cell-padding values live in `split_tree.cpp`, `chrome_host.cpp`, and shader uniforms independently. Font-size changes require hunting down and updating values in 4+ files. This is a maintenance trap.

7. **No timeout in `UiRequestWorker::request_resize`.** An unresponsive Neovim process causes the application to hang indefinitely with no recovery path. For a terminal emulator where the hosted process is user code that can deadlock, this is a reliability issue, not just a code smell.

8. **`ToastHost::active_` unsynchronized access.** The API looks thread-safe (there's a `pending_mutex_`) but isn't — the live display list has no protection. This is the worst kind of bug: it looks correct, it passes basic tests, and it will fail intermittently under load or on hardware with relaxed memory ordering.

9. **Clipboard failure modes silently swallowed.** Clipboard read and write failures log to the debug level but produce no user-visible feedback. In a terminal emulator, silently failing to paste is a significant user-experience defect. The toast infrastructure exists; wiring it up for clipboard errors should have been done at the same time the mutex was added.

10. **Keybinding matching logic split across `InputDispatcher` and `CommandPalette`.** Two independent implementations of chord detection and keybinding lookup means a bug fix or semantic change (e.g., how modifier keys are normalized) must be applied twice and kept in sync manually. The utility function `gui_keybinding_matches()` exists but only partially bridges this gap.

---

*Generated from direct source file inspection of `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/` — April 2026.*
