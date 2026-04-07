**Findings**
1. High: tab/workspace keybindings are not actually first-class config actions. The defaults include `new_tab`, `close_tab`, `next_tab`, `prev_tab`, and `activate_tab:1..9`, but the config serializer and parser allowlists stop at the older 21-action set, so those bindings cannot round-trip through `config.toml` and custom entries for them will be ignored or dropped. See [app_config_io.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp#L33), [app_config_io.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp#L442), [app_config_io.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp#L543), and [keybinding_parser.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/keybinding_parser.cpp#L19).

2. High: the command palette exposes a dead `activate_tab` command. The palette only lists bare action names, but `GuiActionHandler::activate_tab()` is a parameterized action that no-ops without numeric args, so selecting it from the palette does nothing unless the user manually types an argument string. See [command_palette.cpp](/Users/cmaughan/dev/Draxul/app/command_palette.cpp#L29), [command_palette.cpp](/Users/cmaughan/dev/Draxul/app/command_palette.cpp#L177), and [gui_action_handler.cpp](/Users/cmaughan/dev/Draxul/app/gui_action_handler.cpp#L244).

3. Medium-High: `App` is still too large and owns too many unrelated responsibilities for safe parallel work. Startup, font lifecycle, config reload, workspace management, render scheduling, render-test orchestration, dead-pane cleanup, and shutdown persistence all live in one 1577-line file. That raises merge pressure and makes behavioral changes hard to isolate. See [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L153), [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L400), [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L954), and [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L1350).

4. Medium-High: module boundaries are still porous around rendering. `HostManager` reaches into the concrete `MegaCityHost` with `dynamic_cast`, and both `draxul-megacity` and `draxul-nanovg` add renderer-private `src/` include paths. That means renderer-internal refactors still ripple into sibling libraries. See [host_manager.cpp](/Users/cmaughan/dev/Draxul/app/host_manager.cpp#L395), [libs/draxul-megacity/CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/CMakeLists.txt#L80), and [libs/draxul-nanovg/CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-nanovg/CMakeLists.txt#L9).

5. Medium: the config layer still has confusing dependency fan-out. The façade header says to prefer narrower config headers, but the `draxul-config` target publicly links renderer and window, so “config” is not a clean low-level dependency in build terms. That makes layering harder to reason about than the comments suggest. See [app_config.h](/Users/cmaughan/dev/Draxul/libs/draxul-config/include/draxul/app_config.h#L3) and [libs/draxul-config/CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-config/CMakeLists.txt#L15).

6. Medium: documentation drift is now material enough to mislead contributors. `plans/design/renderers.md` still describes `I3DRenderer`/`I3DHost`, `plans/design/city_db.md` says schema version `5` while code is `8`, and `docs/features.md` says the command palette default is `Ctrl + P` while code binds `Ctrl+Shift+P`. See [renderers.md](/Users/cmaughan/dev/Draxul/plans/design/renderers.md#L5), [city_db.md](/Users/cmaughan/dev/Draxul/plans/design/city_db.md#L62), [citydb.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-citydb/src/citydb.cpp#L30), [features.md](/Users/cmaughan/dev/Draxul/docs/features.md#L131), and [app_config_io.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp#L453).

7. Medium: atlas-upload behavior is duplicated across three code paths that all inspect and clear the shared glyph-atlas dirty state. I did not prove a current correctness bug from static inspection, but this is a fragile hotspot because grid hosts, the chrome tab bar, and the command palette each own their own upload logic. See [grid_rendering_pipeline.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp#L228), [chrome_host.cpp](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp#L424), and [command_palette_host.cpp](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L182).

8. Medium: config/font propagation is still active-workspace-biased. `apply_font_metrics()` and `reload_config()` only walk the active `HostManager`, while the app already has all-workspace viewport recomputation helpers. That means inactive tabs are at risk of stale host state after config or font changes. See [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L377), [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L453), and [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L1485).

Static-only review: I did not build, run, or bless anything.

**Top 10 Good**
1. The repo is decomposed into focused libraries instead of one giant target.
2. `IHost`, `IGridRenderer`, and `IGridHandle` are good foundational seams.
3. Test coverage breadth is unusually strong for a GUI/renderer-heavy C++ app.
4. `tests/support/` provides real reusable seams instead of every test inventing its own fakes.
5. The render-tree walk is a clean way to express visibility, order, and deadlines.
6. The grid renderer architecture is efficient and conceptually sound.
7. Shared shader constants in [decoration_constants_shared.h](/Users/cmaughan/dev/Draxul/shaders/decoration_constants_shared.h) are a good anti-drift pattern.
8. Planning discipline is strong: active, complete, and icebox work are clearly separated.
9. Cross-platform concerns are mostly pushed down into platform/backend libraries.
10. The codebase shows sustained refactoring effort, not just feature accumulation.

**Top 10 Bad**
1. A few files are still far too large to review or change comfortably.
2. GUI action metadata is split across multiple registries instead of one source of truth.
3. Renderer-private boundaries are still violated by sibling libraries.
4. MegaCity dominates code volume and complexity relative to the core editor/terminal product.
5. Docs and design notes are stale in places where contributors will trust them.
6. Shared atlas upload behavior is copy-pasted across subsystems.
7. Some cross-workspace behavior is still implemented as “active workspace only.”
8. The config module’s build surface is wider than its API comments imply.
9. Core UI styling decisions are hardcoded in app-layer code instead of being data-driven.
10. Tests are broad, but some important parity and drift invariants still are not encoded.

**Best 10 QoL Features To Add**
1. Editable workspace/tab names instead of deriving labels from host debug names.
2. Move a pane to another tab or extract it into a new tab.
3. Reopen the most recently closed pane/tab.
4. A searchable keybinding inspector with current effective bindings and action descriptions.
5. A pane/tab context menu for close, restart, swap, move, and open-same-host actions.
6. Inline crash/error cards for failed hosts with `Restart`, `Copy Error`, and `Open Log`.
7. “Open current CWD in Finder/Explorer” for shell-backed panes.
8. A quick tab switcher overlay with previews rather than only sequential cycling.
9. Command-palette grouping/descriptions so users do not need to know internal action names.
10. Activity badges on inactive tabs/panes when unseen output arrives.

**Best 10 Tests To Add**
1. A config round-trip test for `new_tab`, `close_tab`, `next_tab`, `prev_tab`, and `activate_tab:1..9`.
2. A registry-parity test that compares runtime GUI actions against config parser/serializer allowlists.
3. A command-palette test that parameterized actions are either hidden or expanded into executable entries.
4. A multi-workspace config/font propagation test covering inactive tabs.
5. An atlas-dirty coordination test with grid host, chrome tabs, and command palette all resolving new glyphs in one cycle.
6. A static boundary test that fails if non-renderer targets include `libs/draxul-renderer/src/...`.
7. A docs/default-binding consistency test for the features table versus `AppConfig` defaults.
8. A render-tree ordering/visibility test for zoom + diagnostics + palette stacking.
9. A dead-host cleanup test for hosts that die in inactive workspaces.
10. A command-palette shortcut-hint test for actions with no binding, chord bindings, and argument-bearing actions.

**Worst 10 Features**
1. MegaCity as a first-class shipped host; it adds huge maintenance cost to the core app.
2. NanoVG demo host exposure in the same host surface as real user modes.
3. Parameterized tab activation encoded as nine pseudo-actions instead of a first-class concept.
4. The command palette surfacing internal action names directly to users.
5. Hardcoded themed chrome/tab visuals in app code.
6. Production CLI surface carrying render-test and screenshot plumbing.
7. Host-specific behavior toggled via `dynamic_cast` from generic management code.
8. Config persistence logic living partly inside specialized hosts and partly in the app.
9. A config façade that drags runtime/window/renderer concepts into “config.”
10. The planning/docs layer presenting stale architecture as if it were current truth.