# Draxul Repository Review

Static review only. I read the current tree under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/` directly from disk and did not build, run, or modify anything.

## Findings

1. **Windows Neovim spawn quoting is still incorrect and can corrupt arguments.**  
   In [`libs/draxul-nvim/src/nvim_process.cpp:49`](/Users/cmaughan/dev/Draxul/libs/draxul-nvim/src/nvim_process.cpp#L49), `quote_windows_arg()` does not implement the full MSVC command-line escaping rules. In particular, trailing backslashes before the closing quote are not doubled, and quote escaping is incomplete. Because [`CreateProcessA` is called with the synthesized command line at lines 113-127](/Users/cmaughan/dev/Draxul/libs/draxul-nvim/src/nvim_process.cpp#L113), paths like `C:\Program Files\nvim\` remain a latent launch/injection bug on Windows.

2. **Multi-pane timing is driven only by the focused host, so unfocused hosts can miss deadlines.**  
   [`app/app.cpp:657-672`](/Users/cmaughan/dev/Draxul/app/app.cpp#L657) computes the next wait deadline from `host_manager_.host()`, which is just the focused host. The main loop pumps every host, but sleep timing only respects one host’s `next_deadline()`. In a split-pane session this can stall cursor blinking, deferred timers, or future timed behavior in unfocused panes until some unrelated event wakes the UI.

3. **`grid_line` replay validates only the starting coordinate, not the full repeated write span.**  
   [`libs/draxul-nvim/src/ui_events.cpp:188-249`](/Users/cmaughan/dev/Draxul/libs/draxul-nvim/src/ui_events.cpp#L188) rejects out-of-range `row` and `col_start`, but then writes `repeat` cells and advances extra columns for wide glyphs without clamping or truncating the batch. The concrete `Grid` swallows out-of-range writes, but the generic `IGridSink` contract is looser, and the handler can still generate invalid calls against alternate sinks and waste work on malformed redraws.

4. **The remaining `dynamic_cast<I3DHost*>` keeps host capabilities implicit and weakly enforced.**  
   [`app/host_manager.cpp:254-260`](/Users/cmaughan/dev/Draxul/app/host_manager.cpp#L254) attaches 3D and ImGui capabilities after initialization via RTTI. That is survivable today, but it means host initialization semantics depend on a post-init side channel instead of an explicit interface contract. This is exactly the kind of hidden coupling that gets missed when multiple agents add host types in parallel.

5. **`HostManager` is materially under-tested relative to its responsibility.**  
   [`tests/host_manager_tests.cpp:7-26`](/Users/cmaughan/dev/Draxul/tests/host_manager_tests.cpp#L7) only verifies shell-kind selection. None of the stateful behavior in [`app/host_manager.cpp`](/Users/cmaughan/dev/Draxul/app/host_manager.cpp) is covered: host creation failure rollback, split/close semantics, focus reassignment, hit-testing, viewport recomputation, or the 3D-host attach path. That is a mismatch between risk and coverage.

6. **The test target still compiles app implementation files directly, which weakens module boundaries.**  
   [`tests/CMakeLists.txt:64-71`](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt#L64) pulls `app/*.cpp` straight into `draxul-tests`. This works, but it means production and test targets each maintain their own app source lists and include conventions. It increases merge friction and makes it easier for app-layer behavior to become “test-only buildable” rather than library-clean.

7. **The agent wrapper scripts are already drifting through copy-paste duplication.**  
   [`scripts/ask_agent.py`](/Users/cmaughan/dev/Draxul/scripts/ask_agent.py), [`scripts/ask_agent_gpt.py`](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gpt.py), [`scripts/ask_agent_claude.py`](/Users/cmaughan/dev/Draxul/scripts/ask_agent_claude.py), and [`scripts/ask_agent_gemini.py`](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gemini.py) repeat path resolution, prompt augmentation, CLI assembly, and subprocess handling with slightly different options. This is already a maintenance hotspot and a collaboration hazard: policy fixes will be applied unevenly.

## Architecture Assessment

The high-level separation is good. `app/` is mostly orchestration, rendering and grid logic are pushed down, and the project clearly values testability. `libs/draxul-grid`, `libs/draxul-app-support`, `libs/draxul-host`, and `libs/draxul-nvim` are the strongest modular seams.

The main structural weaknesses are at the edges:
- `app/` still owns some coordination logic that really belongs in reusable support libraries.
- host capability wiring is still partly implicit
- tests are broad, but some of the highest-risk orchestration paths are shallowly covered
- the scripts/plans layer is organized, but the Python helper tooling is not yet refactored to match the codebase’s otherwise modular style

## Testing Holes

The suite is large, around 525 `TEST_CASE`/`SECTION` declarations, and that is a real strength. The biggest remaining holes are:
- Windows-specific process quoting behavior
- `HostManager` lifecycle/state transitions
- multi-pane timing/deadline behavior
- malformed `grid_line` replay with repeats and wide glyphs near the right edge
- integration coverage around 3D-host capability attachment
- cross-target build wiring drift between app and tests

The manual checklist in [`tests/to-be-checked.md`](/Users/cmaughan/dev/Draxul/tests/to-be-checked.md) is also still carrying important sign-off work, which means several behaviors are not fully automated yet.

## Top 10 Good Things

1. The repository is genuinely modular, with a sensible split between windowing, rendering, fonting, grid, host, and Neovim/RPC concerns.
2. `app/` is thinner than most GUI frontends of this size.
3. The codebase has substantial automated test coverage, including fuzz-style and lifecycle tests.
4. `Grid`, `RendererState`, and `GridRenderingPipeline` are cleanly separated and understandable.
5. The Neovim redraw parser uses a compact dispatch-table approach instead of a sprawling string ladder.
6. Many risky areas already have dedicated regression tests and preserved work-item history.
7. The planning discipline under `plans/` is unusually strong and useful.
8. The project avoids leaking backend-private renderer details into most app code.
9. There is good attention to shutdown behavior, backpressure, and startup rollback.
10. The repo is clearly optimized for iterative engineering rather than demo-only code.

## Top 10 Bad Things

1. Windows process spawning still has a known quoting bug in live code.
2. Multi-pane timing still behaves like a single-host app.
3. `HostManager` owns important lifecycle policy but has minimal behavioral tests.
4. The remaining RTTI-based host capability wiring is implicit and brittle.
5. `grid_line` replay is not defensive enough for repeated/wide writes at boundaries.
6. Tests still compile app source files directly instead of consuming a proper app library.
7. The agent helper scripts are duplicated and likely to drift.
8. `MegaCity` still adds conceptual surface area disproportionate to product value.
9. Diagnostics/state reporting is focused on the active host rather than the whole split session.
10. Some cross-platform edge cases are tracked in plans/manual checklists rather than fully locked down in tests.

## Best 10 Quality-of-Life Features To Add

1. Live config reload.
2. Window state persistence.
3. Per-monitor DPI-aware font scaling policies.
4. Command palette.
5. URL detection and click-open support.
6. Native tab bar.
7. Configurable ANSI palette.
8. Configurable scrollback capacity.
9. Font fallback inspector.
10. Performance HUD.

## Best 10 Tests To Add

1. Windows `quote_windows_arg()` tests for spaces, trailing backslashes, and embedded quotes.
2. `HostManager` create/split/close/focus/hit-test tests with fake hosts.
3. Multi-pane deadline arbitration tests proving unfocused host timers still wake the app.
4. `UiEventHandler::handle_grid_line()` boundary tests for repeated and double-width cells at the right edge.
5. Tests for `HostManager` 3D-host attachment success/failure paths.
6. Tests that `tests/CMakeLists.txt` and the app target cannot silently drift on app source membership.
7. End-to-end split-pane resize tests where all panes get updated viewports.
8. Diagnostics panel tests for multi-host sessions rather than only the focused host.
9. Windows process shutdown timing tests around the fixed 2-second wait/terminate path.
10. Script-level tests for the review helpers so policy flags stay aligned across GPT/Claude/Gemini wrappers.

## Worst 10 Features

1. The `MegaCity` demo host in the production surface area.
2. Post-init RTTI-based 3D host attachment.
3. Direct `open_file:` action-string dispatch between UI and host layers.
4. Focused-host-only timing/deadline handling.
5. Focused-host-only diagnostics in a split-pane application.
6. Direct compilation of app implementation units into the test target.
7. Duplicated multi-agent wrapper scripts.
8. Manual verification dependence for several important regressions.
9. Windows-only hand-rolled command-line quoting.
10. Hidden capability coupling between host types and renderer/ImGui wiring.

## Overall

This is a strong codebase with better separation and test discipline than most desktop frontends. The major issues are not “mess everywhere”; they are concentrated in a few high-leverage seams: Windows spawn correctness, multi-pane orchestration, `HostManager` test depth, and helper-script maintainability. If those are tightened, the project becomes much easier for multiple agents to extend safely in parallel.