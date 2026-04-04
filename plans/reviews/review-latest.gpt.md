# Draxul Repository Review

Static review only. I did not build, run tests, or execute project binaries.

## Summary

The codebase is better structured than most fast-moving C++ desktop apps: the library split is real, test coverage is broad, and there is clear planning discipline. The main architectural debt is now concentrated rather than hidden: `app/` is still a coordination choke point, `draxul-config` still knows too much about runtime/UI concerns, and Megacity continues to punch holes through otherwise good module boundaries.

## Findings

- **High:** Megacity still bypasses renderer encapsulation by adding private backend include paths directly in build wiring, which means renderer refactors will keep spilling across target boundaries and forcing coordination across unrelated work. See [libs/draxul-megacity/CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/CMakeLists.txt#L80) and [libs/draxul-megacity/CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/CMakeLists.txt#L89).

- **High:** Tests also depend on Megacity private internals by adding `libs/draxul-megacity/src` to the test include path. That makes tests part of the implementation surface and weakens the repo’s ability to refactor safely with multiple agents in parallel. See [tests/CMakeLists.txt](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt#L19).

- **High:** GUI action definitions are duplicated across three separate registries instead of one source of truth: config parsing, keybinding parsing, and runtime dispatch. Every new action requires synchronized edits in different libraries, which is exactly the kind of drift-prone work that causes coordination bugs. See [libs/draxul-config/src/app_config_io.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp#L33), [libs/draxul-config/src/keybinding_parser.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/keybinding_parser.cpp#L19), and [app/gui_action_handler.cpp](/Users/cmaughan/dev/Draxul/app/gui_action_handler.cpp#L21).

- **Medium:** `draxul-config` is still not a clean config library. Its public `AppOptions` type exposes renderer and window types, and the target links renderer/window publicly, so “config” consumers inherit UI/runtime baggage and rebuild fan-out. See [libs/draxul-config/include/draxul/app_options.h](/Users/cmaughan/dev/Draxul/libs/draxul-config/include/draxul/app_options.h#L3) and [libs/draxul-config/CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-config/CMakeLists.txt#L15).

- **Medium:** `App` remains a 1,249-line change magnet that owns startup, shutdown, the main loop, render-test orchestration, config reload, overlay composition, viewport logic, and cross-subsystem callbacks. The code is readable, but it is still a conflict hotspot that limits parallel work. See [app/app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L152), [app/app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L671), [app/app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L894), and [app/app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp#L1207).

- **Medium:** `CommandPaletteHost` reimplements atlas dirty-rect upload logic instead of sharing the same upload path used by the normal grid rendering pipeline. That creates two subtly different atlas-update code paths to maintain. See [app/command_palette_host.cpp](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L181) and [libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp#L227).

- **Medium:** CLI parsing in `main.cpp` mixes pure parsing with process termination via `std::exit()`. That makes argument handling harder to unit test and harder to reuse cleanly from other entrypoints or tooling. See [app/main.cpp](/Users/cmaughan/dev/Draxul/app/main.cpp#L93).

- **Medium:** The agent helper scripts are copy-pasted variants with repeated path resolution, prepend-file handling, dry-run output, and subprocess setup. This is classic tooling drift territory and there are no tests around it. See [scripts/ask_agent.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent.py#L12), [scripts/ask_agent_gpt.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gpt.py#L11), [scripts/ask_agent_claude.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent_claude.py#L11), and [scripts/ask_agent_gemini.py](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gemini.py#L11).

- **Low:** `CellText` still hard-truncates clusters at 32 bytes in a core shared type. The warning and tests are better than silent corruption, but this is still a permanent representational limit in a Unicode-heavy product. See [libs/draxul-grid/include/draxul/grid.h](/Users/cmaughan/dev/Draxul/libs/draxul-grid/include/draxul/grid.h#L124).

## Top 10 Good

- The library split is real enough that most subsystems have a visible home and a named responsibility.
- Test coverage is unusually broad for a native GUI/terminal app.
- The replay fixture approach for redraw bugs is a strong investment in regression testing.
- Fake window/renderer/host seams make the app layer much more testable than average.
- Planning hygiene is good: active, icebox, complete, prompt, and review folders are clear and usable.
- The codebase documents implemented features explicitly in [docs/features.md](/Users/cmaughan/dev/Draxul/docs/features.md).
- Cross-platform abstractions are present instead of being scattered through the whole tree.
- Logging and perf instrumentation are consistent and widespread.
- The render test and snapshot infrastructure gives the project a useful visual regression safety net.
- Recent refactors have clearly reduced some earlier monoliths; the repo is trending in the right direction.

## Top 10 Bad

- Megacity consumes too much architectural budget for an optional demo host.
- `App` is still the dominant merge-conflict zone.
- `draxul-config` is still partly a runtime/UI library in disguise.
- GUI action metadata is duplicated instead of centralized.
- Private implementation boundaries are still leaking into builds and tests.
- Tooling scripts are duplicated and mostly untested.
- Several important flows still rely on stringly-typed action protocols.
- Core text storage still embeds a hard 32-byte truncation limit.
- `SdlWindow` and `InputDispatcher` are still large mixed-responsibility classes.
- Some low-level hygiene drift is visible, such as duplicated includes in [libs/draxul-types/include/draxul/types.h](/Users/cmaughan/dev/Draxul/libs/draxul-types/include/draxul/types.h#L1).

## Best 10 QoL Features To Add

- Command palette descriptions, examples, and argument hints so actions are discoverable without source knowledge.
- A searchable keybinding help screen that explains current bindings and conflicts.
- Per-pane titles and status badges showing host type, cwd, and busy state.
- Clipboard history with palette-driven paste selection.
- Recent files support, especially for dropped/opened paths.
- Clone/duplicate focused pane with the same host kind and working directory.
- Quick layout presets for common setups like editor-plus-shell or three-pane review mode.
- Safe close/restart prompts when a pane appears to have running work or dirty editor state.
- Theme preset import/export with live preview.
- Palette actions for “copy cwd”, “copy current file path”, and “open cwd in Finder/Explorer”.

## Best 10 Tests To Add

- A parity test that enforces one-to-one agreement between GUI action dispatch, config parsing, and keybinding parsing.
- Dry-run tests for the `ask_agent*` and `do_review*` scripts so the review tooling stops depending on manual verification.
- A repeated open/close/reopen lifecycle test for `CommandPaletteHost` to catch leaked handles or stale atlas state.
- Command palette tests for whitespace-heavy queries, argument passing, and tab-completion edge cases.
- An `App::dispatch_to_nvim_host` test covering both reuse of an existing Nvim pane and fallback creation of a new one.
- A `reload_config` rollback test that verifies failed font reloads leave host state and config state coherent.
- A `HostManager::split_host_kind_for()` matrix test across primary host kinds and platform defaults.
- `NvimHost::dispatch_action("open_file_at_function:...")` tests for spaces, unicode, and delimiter-like characters.
- `RendererBundle` tests that verify capability pointers are set and cleared correctly across reset paths.
- Logging configuration tests for environment precedence, category masks, and stderr/file behavior.

## Worst 10 Features

- The MegaCity host.
- The MegaCity debug-view matrix.
- The MegaCity LCOV/perf coverage visualization layer.
- The MegaCity city-map dependency routing surface.
- The MegaCity tree and park tuning surface.
- The global diagnostics panel as a runtime-coupled control hub.
- The `edit_config` action opening a new Nvim split instead of offering structured settings UX.
- The `open_file:` and `open_file_at_function:` string action protocol.
- The screenshot/render-test/bless flags living in the main app binary instead of a thinner external harness.
- The platform-default split-host behavior, which is convenient but not very intentional from a workflow perspective.

## Overall

The repo is in better shape than the feature count suggests. The next real gains will not come from more surface area; they will come from finishing the boundary cleanup that the architecture already points toward: centralize action metadata, finish purifying config/runtime seams, stop private-source includes in Megacity and tests, and keep shrinking the responsibilities of `App`.