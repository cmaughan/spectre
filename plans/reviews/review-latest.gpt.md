# Static Review Report

I reviewed the current repository state directly from disk under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/`. I did not build, run tests, or execute project binaries.

## Executive Summary

This is a strong codebase with better modularity and test discipline than most desktop frontends. The current risks are not “the whole architecture is wrong”; they are concentrated in a few high-leverage seams:

- config/runtime boundaries are still too entangled
- the app layer still knows too much about concrete host/window capabilities
- some of the most important tests are shallow or mirror implementation instead of exercising it
- repo automation and planning files have drifted enough to create friction for parallel agent work

I did not find a single obvious high-confidence crash bug from static inspection alone. The bigger problem is architecture drift plus a few false-comfort tests around critical integration paths.

## Findings

1. **High: the config boundary is still collapsed, despite the “split” facade claiming otherwise.**  
`[app_config.h](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/include/draxul/app_config.h#L3)` says new code should use `app_config_types.h` for “struct definitions only”, but `[app_config_types.h](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/include/draxul/app_config_types.h#L3)` still pulls in `[renderer.h](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/include/draxul/renderer.h)`, `[text_service.h](/Users/cmaughan/dev/Draxul/libs/draxul-font/include/draxul/text_service.h)`, and `[window.h](/Users/cmaughan/dev/Draxul/libs/draxul-window/include/draxul/window.h)`, and it also co-locates persistent `AppConfig` with runtime/test-only `AppOptions` factory seams at `[app_config_types.h](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/include/draxul/app_config_types.h#L99)`. That means config work still drags renderer/window/runtime dependencies and rebuilds with it.

2. **High: the app layer still performs RTTI-based capability dispatch and concrete implementation probing.**  
`[app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L24)` uses `dynamic_cast<SdlWindow*>` for render-test behavior, and `[app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L307)` plus `[app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L457)` do repeated `dynamic_cast<I3DHost*>` checks for ImGui/font wiring. This keeps lower-layer capability knowledge in the orchestration layer and weakens compile-time guarantees when adding new host/window types.

3. **High: test build wiring is still merge-conflict-prone for multi-agent work.**  
`[tests/CMakeLists.txt](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt#L6)` is one long hand-maintained list of every test file, and `[tests/CMakeLists.txt](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt#L73)` adds private include roots from `app/` and `libs/draxul-window/src`. This makes unrelated test additions collide in the same file and encourages tests to couple to non-public internals.

4. **Medium: file-drop coverage is mostly a mirrored-string test, not a production-path test.**  
`[file_drop_tests.cpp](/Users/cmaughan/dev/Draxul/tests/file_drop_tests.cpp#L35)` reimplements the encoding/decoding logic locally, while production behavior lives in `[input_dispatcher.cpp](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L243)` and `[nvim_host.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L161)`. The comments are accurate, but this test will still pass if the wiring between the real components breaks.

5. **Medium: the risky `InputDispatcher` state machine is under-tested.**  
The real behavior includes prefix handling, text suppression, mouse routing, pixel-scale conversion, and file-drop dispatch in `[input_dispatcher.h](/Users/cmaughan/dev/Draxul/app/input_dispatcher.h#L109)` and `[input_dispatcher.cpp](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L209)`. But `[input_dispatcher_routing_tests.cpp](/Users/cmaughan/dev/Draxul/tests/input_dispatcher_routing_tests.cpp#L25)` mostly constructs the dispatcher with null deps and only checks `gui_action_for_key_event()`. The most failure-prone logic is barely exercised.

6. **Medium: DPI testing is still mostly arithmetic testing, not orchestration testing.**  
`[dpi_scaling_tests.cpp](/Users/cmaughan/dev/Draxul/tests/dpi_scaling_tests.cpp#L19)` verifies formulas like `96 * scale` and `compute_panel_layout`, plus `TextService` reinit. It does not cover `App::on_display_scale_changed()` in `[app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L582)`, where the real risk is coordinating font rebuild, ImGui texture rebuild, viewport recompute, and `InputDispatcher` pixel-scale sync.

7. **Medium: `HostManager` tests still miss the capability paths that justify its complexity.**  
The harness in `[host_manager_tests.cpp](/Users/cmaughan/dev/Draxul/tests/host_manager_tests.cpp#L24)` only creates a plain `IHost`, via `[host_manager_tests.cpp](/Users/cmaughan/dev/Draxul/tests/host_manager_tests.cpp#L151)`. There is no coverage for `I3DHost` attachment, no check that capability wiring happens exactly once, and no test around mixed host types or host ImGui traversal.

8. **Medium: planning automation is stale enough to mislead tools and reviewers.**  
`[consensus_review.md](/Users/cmaughan/dev/Draxul/plans/prompts/consensus_review.md#L1)` points at `plans/reviews/_latest_` and asks for `review-concensus.md`; neither matches the current repository layout/spelling. `plans/README.md` documents collision handling in `[plans/README.md](/Users/cmaughan/dev/Draxul/plans/README.md#L38)`, but the actual icebox still contains duplicate variants like `20 searchable-scrollback -feature.md` and `20 searchable-scrollback -feature 1.md`.

9. **Low: agent helper scripts are duplicated enough that policy drift is already happening.**  
`[ask_agent.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent.py)`, `[ask_agent_gpt.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gpt.py)`, `[ask_agent_claude.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent_claude.py)`, and `[ask_agent_gemini.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gemini.py)` repeat path resolution, prompt stitching, and command assembly. The wrappers are similar but not unified, which will get worse as policies evolve.

10. **Low: comment drift already exists in `InputDispatcher`.**  
The contract comment in `[input_dispatcher.h](/Users/cmaughan/dev/Draxul/app/input_dispatcher.h#L23)` says text/editing events go to `UiPanel` and host, but `[input_dispatcher.cpp](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L224)` only forwards text editing to the host. That is small now, but it is exactly how IME-related regressions get introduced later.

## Module Layout Assessment

The overall library split is good:

- `draxul-types`, `draxul-grid`, `draxul-window`, `draxul-renderer`, `draxul-font`, `draxul-nvim`, and `draxul-host` are sensible units.
- `GridRenderingPipeline` sitting below app level is the right move.
- The renderer hierarchy is materially cleaner than the older “callback hack” design.
- `PIMPL` use in text/rpc/process code is disciplined.

Where the layout still slips:

- config types and runtime/test seams are mixed together
- app still reaches into capability details instead of owning only orchestration
- tests still need private include paths to get work done
- planning/tooling files are not held to the same quality bar as core code

## Testing Assessment

Strengths:

- Broad test surface across VT parsing, RPC, config, scrollback, rendering state, shutdown, and host lifecycle
- Useful replay fixture pattern for redraw bugs
- Good use of fake renderer/window seams to avoid hard runtime dependencies

Gaps:

- too many tests validate local mirrors of logic instead of the integrated path
- critical app-level orchestration seams are still only indirectly covered
- multi-host capability behavior is not tested deeply enough
- some tests are large enough to become their own maintenance burden

## Top 10 Good Things

1. The library decomposition is real, not cosmetic.
2. The renderer interface hierarchy is much cleaner than the typical desktop-GUI rendering stack.
3. `GridRenderingPipeline` keeps rendering translation out of the app layer.
4. `TextService`, `NvimProcess`, and `NvimRpc` hide heavy implementation details well.
5. The project has unusually broad automated test coverage for a native GUI app.
6. The replay fixture support is a strong investment in regression reproduction.
7. Defensive bounds checks in redraw/grid code are visible and intentional.
8. The `Deps`/factory seams make a lot of core code testable without SDL/GPU/Neovim.
9. Planning history is preserved well enough to understand why many refactors happened.
10. Cross-platform concerns are mostly pushed downward into the correct libraries.

## Top 10 Bad Things

1. Config types, runtime options, and test-only factories are still bundled together.
2. The app layer still contains capability probing that belongs lower down.
3. Test registration and private include wiring are conflict-prone.
4. Some important tests are implementation mirrors instead of integration tests.
5. The input routing state machine is more complex than its coverage suggests.
6. DPI behavior is tested as math more than as application behavior.
7. Planning prompts and review automation have drifted out of sync with the tree.
8. Agent helper scripts are duplicated and already diverging in behavior.
9. A few hotspot files remain too large for easy parallel modification.
10. Repo hygiene in `plans/` is weaker than code hygiene in `libs/`.

## Best 10 Quality-of-Life Features To Add

1. Session persistence for pane layout, host kinds, and working directories.
2. Live config reload for non-structural settings.
3. Searchable scrollback with highlight/navigation.
4. URL detection and click-to-open inside terminal buffers.
5. Window state persistence including size, position, and maximized state.
6. Command palette for discoverability of actions and hosts.
7. Per-pane environment and working-directory overrides.
8. Remote Neovim attach.
9. Configurable ANSI palette and richer terminal theming.
10. Native tab bar or workspace/session switching on top of split panes.

## Best 10 Tests To Add

1. `HostManager` tests for `I3DHost` attach/detach behavior and exactly-once registration.
2. End-to-end `InputDispatcher` chord-prefix tests through `connect()` with fake window/ui/host.
3. End-to-end file-drop/open-file tests through `InputDispatcher` into `NvimHost::dispatch_action`.
4. App-level DPI hotplug integration tests covering font rebuild, ImGui rebuild, viewport recompute, and pixel-scale sync.
5. `App::render_imgui_overlay()` tests with mixed plain hosts and `I3DHost` hosts.
6. `SdlWindow` custom-event tests for wake events, file-dialog completion, and display-scale events.
7. Multi-pane close/focus tests that include host callbacks firing during shutdown.
8. Cross-host split behavior tests where the focused pane’s kind differs from the primary host’s kind.
9. Header-boundary tests ensuring config-only code does not need renderer/window headers.
10. Smoke-level tests for planning/review helper scripts and prompt-path validity.

## Worst 10 Features

These are the weakest current user-facing features or feature areas, not necessarily the most buggy:

1. The MegaCity cube demo in the production surface area.
2. Split panes without persistence or recovery.
3. File-open integration that is really only useful for Neovim panes.
4. The diagnostics panel as a sparse metrics dump rather than a real control surface.
5. Chord-based pane splitting with little discoverability.
6. Smooth scrolling with a very simple accumulator model.
7. Terminal theming limited to only fg/bg overrides.
8. Host selection being mostly a startup-time concern instead of a first-class workflow.
9. Multi-pane behavior that still feels secondary to the single-host path.
10. Review/planning automation as a user-visible workflow feature, because it is currently too brittle.

## Closing Note

The codebase is in good shape overall. The next gains are less about rewriting core subsystems and more about tightening the seams that multiple contributors will keep touching: config boundaries, app/capability layering, end-to-end tests for routing/orchestration, and repo automation hygiene.

No binaries were run and no tests were executed, per instruction.