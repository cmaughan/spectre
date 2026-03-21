# Draxul Repository Review

Static inspection only. I did not build, run, or modify anything.

## Executive Summary

The repo is better-structured than most GUI frontends: the library split is real, the tests are unusually strong, and the work-item history shows deliberate refactoring. The main drag now is not lack of intent, it is concentration of responsibility. A few files and interfaces still carry too much policy, which makes parallel work slower and increases the chance of subtle regressions.

The two most concrete correctness issues I found are in the file-open path and the GUI action registry. The biggest structural issues are the `TerminalHostBase` god object, the unbounded Neovim notification queue, and public headers that pull in more subsystem detail than they should.

## Findings

1. **Dropped/open-dialog file paths are passed to Neovim unsafely.**  
   [`input_dispatcher.cpp`](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L111) forwards raw paths as `"open_file:" + path`, and [`nvim_host.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L156) turns that into `nvim_command("edit " + path)`. Paths with spaces, quotes, `|`, `%`, or command-like characters can misparse or do the wrong thing. This should use an escaped filename or Lua/API-based open path.

2. **`open_file_dialog` is implemented but not configurable through `config.toml`.**  
   [`gui_action_handler.cpp`](/Users/cmaughan/dev/Draxul/app/gui_action_handler.cpp#L50) recognizes `open_file_dialog`, but [`app_config.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/src/app_config.cpp#L26) does not include it in `kKnownGuiActions`, so [`parse_gui_keybinding`](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/src/app_config.cpp#L454) rejects it. That leaves the feature half-integrated.

3. **The Neovim notification queue is still unbounded.**  
   [`rpc.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-nvim/src/rpc.cpp#L23) stores notifications in a plain `std::vector`, drained opportunistically at [`rpc.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-nvim/src/rpc.cpp#L175). The repo’s own design note calls this out as a known risk in [`belt-and-braces.md`](/Users/cmaughan/dev/Draxul/plans/design/belt-and-braces.md#L7). Under redraw bursts or temporary UI stalls, backlog growth is unconstrained.

4. **`TerminalHostBase` remains the main architectural bottleneck.**  
   [`terminal_host_base.h`](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/terminal_host_base.h#L3) pulls in alt-screen, scrollback, selection, mouse, parser, VT state, highlighting, and process lifecycle into one inheritance root. That makes the shell/PowerShell path hard to split safely across multiple agents and keeps tests focused on one large behavior surface instead of smaller composable units.

5. **The config/public API boundary is still too heavy.**  
   [`app_config.h`](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/include/draxul/app_config.h#L3) exposes SDL types, renderer types, and `TextService` constants from what should be mostly data/config plumbing. [`draxul-app-support/CMakeLists.txt`](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/CMakeLists.txt#L24) also links `draxul-nvim` and `draxul-renderer` publicly, which spreads those dependencies transitively.

6. **`UiPanel` is still a monolith.**  
   [`ui_panel.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-ui/src/ui_panel.cpp#L1) mixes styling, layout, window composition, metric rendering, dockspace creation, SDL-to-ImGui key translation, input forwarding, and lifecycle management in one 758-line file. That is workable today, but it is poor parallel-edit territory.

7. **`SdlWindow` mixes too many concerns in one implementation file.**  
   [`sdl_window.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_window.cpp#L20) combines DPI diagnostics, platform activation hacks, SDL event translation, clipboard, async file dialog marshalling, and window creation/shutdown. It is test-hostile and makes platform work noisier than it needs to be.

8. **Some tests validate copied logic instead of production paths.**  
   [`clipboard_tests.cpp`](/Users/cmaughan/dev/Draxul/tests/clipboard_tests.cpp#L10) explicitly mirrors production logic instead of calling the real implementation. That can keep tests green while the actual path drifts.

9. **The test harness is still split between Catch auto-registration and a manual call list.**  
   [`test_main.cpp`](/Users/cmaughan/dev/Draxul/tests/test_main.cpp#L9) declares a long manual suite list, then runs Catch separately at [`test_main.cpp`](/Users/cmaughan/dev/Draxul/tests/test_main.cpp#L50). This is maintainable only as long as people remember both registration styles.

10. **Planning/review artifacts have started to drift from the source tree.**  
   [`review-consensus.md`](/Users/cmaughan/dev/Draxul/plans/reviews/review-consensus.md#L1) is not a normal consensus note; it is a wrapper-style status message about what was produced. Also, `plans/work-items/` is currently empty while the repo conventions still talk as if it is the active backlog. That weakens the repo as a coordination surface.

## Architecture Notes

The repo’s intended dependency direction is mostly respected in spirit. `app/` is thinner than before, renderer backends are private, and pure logic such as grid, highlight handling, VT parsing, and codec work is testable. The remaining issue is that several “base” abstractions still carry subsystem ownership instead of just protocol ownership. The main examples are [`GridHostBase`](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/grid_host_base.h#L15), [`TerminalHostBase`](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/terminal_host_base.h#L23), [`IWindow`](/Users/cmaughan/dev/Draxul/libs/draxul-window/include/draxul/window.h#L11), and the 3D injection path in [`renderer.h`](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/include/draxul/renderer.h#L71).

The MegaCity path is still the least coherent part of the architecture. [`renderer.h`](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/include/draxul/renderer.h#L85) exposes opaque `void*` draw callbacks, [`renderer.h`](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/include/draxul/renderer.h#L128) uses `dynamic_cast` to surface them, and [`megacity_host.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_host.cpp#L12) continues the opaque-state pattern. It is isolated, but it still bends core renderer abstractions around a demo host.

## Testing Holes

- No real integration test covers dropped/open-dialog file paths with spaces or special characters through the actual `InputDispatcher -> NvimHost -> nvim_command` path.
- No test covers the `open_file_dialog` action being bindable from config, because today it is not.
- No direct test exercises `SdlWindow` event translation, async file-dialog event marshalling, or activation behavior.
- No test covers `main.cpp` argument parsing, especially render-test flags and host selection.
- No test validates overload behavior of the Neovim notification queue because there is no bounded/coalesced policy yet.
- Diagnostics panel tests cover layout/input well, but not higher-level window composition drift or metric presentation.
- The mixed manual/Catch harness itself is untested and easy to partially bypass.
- Script/review plumbing under `scripts/` and `plans/` has very little self-validation despite being part of the repo’s collaboration workflow.

## Top 10 Good Things

1. The library split is meaningful, not cosmetic.
2. Tests are broad for a GUI app, especially around grid, RPC, VT, DPI, startup rollback, and rendering state.
3. [`replay_fixture.h`](/Users/cmaughan/dev/Draxul/tests/support/replay_fixture.h#L1) is a strong seam for redraw bugs.
4. Renderer state and shader layout are protected with explicit static assertions.
5. `App` initialization has rollback discipline instead of ad hoc cleanup.
6. Host abstractions made shell, PowerShell, and Neovim support feasible without forking the whole app.
7. Render-test scenarios and reference images give the project a concrete visual regression story.
8. The work-item history in `plans/` preserves architectural context unusually well.
9. Dependency cleanup has clearly happened over time; the tree is better than its history implies.
10. Logging categories and diagnostics instrumentation are stronger than average for a project this size.

## Top 10 Bad Things

1. The file-open path is unsafe and under-tested.
2. `TerminalHostBase` is still a god class.
3. The Neovim notification queue is unbounded.
4. `MegaCity` still distorts renderer abstractions.
5. `app_config.h` leaks SDL and renderer/font concerns into public config surface.
6. `UiPanel` and `SdlWindow` are still large edit-conflict magnets.
7. Some tests mirror implementation instead of exercising it.
8. The test harness uses two registration models.
9. Planning artifacts have drifted and are not consistently repo-facing.
10. Collaboration scripts under `scripts/` are visibly duplicated and lightly factored.

## Best 10 Features To Add For Quality Of Life

1. Command palette for app actions and common host commands.
2. Live config reload with visible parse errors.
3. Window/session state persistence.
4. Configurable ANSI palette and theme presets.
5. URL detection and clickable links.
6. Remote Neovim attach in addition to local spawn.
7. IME composition UI and candidate positioning polish.
8. Font fallback inspector in the diagnostics panel.
9. Searchable scrollback/history view.
10. Better file-open UX with recent files and safe path handling.

## Best 10 Tests To Add For Stability

1. End-to-end open-file test for paths with spaces, quotes, Unicode, and shell-like characters.
2. Config parse/serialize test proving `open_file_dialog` bindings round-trip once added.
3. Stress test for bounded/coalesced RPC notifications under redraw flood.
4. Extracted SDL event translation tests for `SdlWindow`.
5. `main.cpp` argument parsing table tests.
6. Integration test for save-on-shutdown ordering and config persistence edge cases.
7. Diagnostics panel composition/snapshot tests at multiple DPIs and panel visibility states.
8. Cross-platform file-dialog result marshalling test with cancellation and callback ordering.
9. Real clipboard-provider integration test through `NvimHost`, not mirrored helpers.
10. Refactor-guard test ensuring all non-Catch suites are either auto-registered or intentionally listed.

## Worst 10 Features

1. `MegaCity` as a first-class host path.
2. Raw `:edit {path}` file opening.
3. The unbounded RPC notification backlog behavior.
4. The mixed manual/Catch test registration model.
5. Bottom-panel diagnostics coupled directly to the main app/render loop.
6. Public config surface depending on SDL keycodes and renderer/font internals.
7. Opaque `void*` 3D renderer callback plumbing.
8. Native window activation hacks living directly in the SDL window implementation.
9. Review/planning files that describe generated output instead of repository truth.
10. Duplicated agent-wrapper scripts that will drift independently.

If you want a follow-up, the best next step is a prioritized remediation plan. My recommended order would be: file-open safety, `open_file_dialog` config integration, bounded/coalesced RPC notifications, `TerminalHostBase` decomposition, then `app_config`/`draxul-app-support` dependency slimming.