Static inspection only of `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/`; no builds or tests were run. No obvious high-severity correctness bug jumped out from the current tree, but there are several maintainability hotspots and a few thinly tested complex areas.

**Findings**
1. Medium: `RendererState` exposes a fairly rich partial-upload contract, but both backends ignore it and memcpy the full packed state every upload. That leaves misleading API surface and unnecessary complexity in a hot path. See [renderer_state.h](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/include/draxul/renderer_state.h):93, [vk_renderer.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/vulkan/vk_renderer.cpp):106, [metal_renderer.mm](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm):107.
2. Medium: The config layer still leaks renderer/window/font and optional-demo concerns despite the header comment promising the opposite. `draxul-config` publicly links those subsystems, `app_config_io.cpp` includes SDL and `TextService`, and core `AppConfig` carries `MegaCityCodeConfig`. That makes small config work fan out across unrelated modules. See [app_config_types.h](/Users/cmaughan/dev/Draxul/libs/draxul-config/include/draxul/app_config_types.h):3, [CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-config/CMakeLists.txt):14, [app_config_io.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp):3, [app_config_types.h](/Users/cmaughan/dev/Draxul/libs/draxul-config/include/draxul/app_config_types.h):150.
3. Medium: MegaCity is still the biggest architectural outlier. Its public host header pulls in `CityDatabase` and `CodebaseScanner`, its CMake wiring reaches into renderer-private headers, and the host implementation mixes input, background scan, DB reconcile, scene building, and ImGui in one large file. Optional demo code still has too much structural reach. See [megacity_host.h](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/include/draxul/megacity_host.h):4, [CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/CMakeLists.txt):60, [megacity_host.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_host.cpp):559.
4. Low-Medium: Font resolution remains a monolithic inline header implementation. Platform probing, filesystem heuristics, font loading, and shaper setup all sit in a transitive include, which increases rebuild cost and creates a merge hotspot for unrelated font work. See [font_resolver.h](/Users/cmaughan/dev/Draxul/libs/draxul-font/src/font_resolver.h):149, [font_resolver.h](/Users/cmaughan/dev/Draxul/libs/draxul-font/src/font_resolver.h):164.
5. Low-Medium: Coverage is uneven where concurrency and persistence are most subtle. `CodebaseScanner` documents a brittle “start once” lifecycle and runs a background thread, but currently has one focused test; `CityDatabase` also has essentially one focused reconcile test despite schema, reopen, and idempotence risk. See [treesitter.h](/Users/cmaughan/dev/Draxul/libs/draxul-treesitter/include/draxul/treesitter.h):55, [treesitter.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-treesitter/src/treesitter.cpp):257, [treesitter_tests.cpp](/Users/cmaughan/dev/Draxul/tests/treesitter_tests.cpp):47, [citydb_tests.cpp](/Users/cmaughan/dev/Draxul/tests/citydb_tests.cpp):64.
6. Low: Review/process artifact hygiene is not fully trustworthy right now. `plans/reviews/review-consensus.md` is not an actual consensus note; it is meta-output claiming one was written. That weakens the planning workflow for humans and agents alike. See [review-consensus.md](/Users/cmaughan/dev/Draxul/plans/reviews/review-consensus.md):1.

**Top 10 Good**
- The library split is strong for a C++ GUI app, and the dependency direction is clearly documented.
- The host and renderer capability hierarchies are much cleaner than a typical ad hoc platform abstraction.
- `GridHostBase`, `SelectionManager`, `ScrollbackBuffer`, `CursorBlinker`, and `UiRequestWorker` are good examples of concern separation.
- The render-test scenario system is a serious asset for a graphics-heavy codebase.
- `tests/support/replay_fixture.h` is a very good seam for redraw/parser regression tests.
- The codebase has a real regression culture: many historical bugs were turned into tests instead of tribal knowledge.
- Shader/layout contracts are guarded with shared constants and static asserts, which is the right instinct.
- Diagnostics, startup timing capture, and structured logging are practical and developer-friendly.
- Cross-host support is ambitious and mostly kept behind coherent abstractions.
- The repo docs and agent instructions are unusually specific, which helps both humans and automated contributors.

**Top 10 Bad**
- MegaCity still concentrates too much complexity in one optional subsystem.
- Core config is still broader than it should be and pulls optional concerns into common paths.
- Renderer dirty-region APIs currently over-promise relative to production use.
- Some files are still large enough to be merge-conflict magnets.
- Font resolution is compile-heavy and not especially modular.
- Tree-sitter and city DB complexity are under-tested relative to risk.
- Important behaviors still live in a manual checklist rather than automation.
- Planning/review artifacts are not all reliable source-of-truth documents.
- Tooling scripts appear useful but largely untested.
- There are still small hygiene leftovers like duplicate includes and manual ownership patterns.

**10 QoL Features To Add**
- OSC 8 hyperlink support for explicit clickable links from terminal apps.
- Shell integration prompt marks and command boundaries, e.g. OSC 133-style navigation.
- Keyboard-driven copy mode for terminal panes, not just mouse-first selection.
- Pane labels plus a quick pane switcher.
- “Equalize panes” and “maximize focused pane” actions.
- Recent files and recent working directories picker.
- Scrollback export/save for the focused pane.
- Focused-pane-aware open-file flow, including “open here” vs “open in split”.
- Named launch profiles for common hosts, args, cwd, and startup commands.
- Non-blocking toast notifications for recoverable events like clipboard or spawn failures.

**10 Tests To Add**
- `CodebaseScanner` restart test: `start -> stop -> start` should behave safely and publish fresh snapshots.
- `CodebaseScanner` skip test for hidden dirs, build dirs, and Objective-C sources.
- `CodebaseScanner` parse-error cap test to verify bounded error collection and completed snapshots.
- `CityDatabase` idempotent reconcile test covering deleted and renamed symbols across snapshots.
- `CityDatabase` reopen/move/close semantics test.
- `FontResolver` style-font auto-detection heuristic test.
- `MegaCityHost` degraded init/shutdown test when city DB open fails and sign text service is unavailable.
- `MegaCityHost` unchanged-snapshot test to ensure reconcile/rebuild does not loop needlessly.
- SDL file-dialog event ownership test for `sdl_file_dialog.cpp`.
- `App::run_render_test` settle/quiet-state logic test.

**Worst 10 Current Features**
- MegaCity host overall: too much cost for too little core-product value.
- MegaCity live scanner/database pipeline: clever, but structurally expensive.
- MegaCity’s config tuning surface: too many knobs leaking into common config.
- Remote clipboard interoperability: still incomplete for terminal-heavy remote workflows.
- Selection/clipboard UX: functional, but still constrained and mouse-centric.
- Native file dialog flow: minimal and not very context-aware.
- Diagnostics panel as a user feature: useful data, limited actionability.
- Scrollback UX as exposed today: history exists, but the ergonomics are still basic.
- Font auto-discovery/fallback UX: capable, but opaque and heuristic-heavy.
- Pane management UX beyond simple splits: functional foundation, weak finishing layer.