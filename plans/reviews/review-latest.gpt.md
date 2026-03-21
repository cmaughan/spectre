# Repository Review

## Findings

- High: failed startup can still overwrite the user config. `App::initialize()` arms a rollback that calls `shutdown()` on any init failure, and `shutdown()` persists config whenever `save_user_config` is true. That means a failed window/renderer/font/host init can still write partial/default state back to disk. See [app/app.cpp#L73](/Users/cmaughan/dev/Draxul/app/app.cpp#L73) and [app/app.cpp#L542](/Users/cmaughan/dev/Draxul/app/app.cpp#L542).

- Medium: mouse-event modifier state is sampled from global SDL state, not from the queued event being translated. For delayed or bursty input, Ctrl/Shift/Alt state can be wrong for click, drag, and wheel handling. See [sdl_event_translator.cpp#L32](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_event_translator.cpp#L32).

- Medium: clipboard tests are drift-prone because they reimplement production logic instead of calling it. The tests even still refer to `App` in comments, but the real logic now lives in `NvimHost`. A future change in the host path can silently bypass these tests. See [clipboard_tests.cpp#L9](/Users/cmaughan/dev/Draxul/tests/clipboard_tests.cpp#L9), [nvim_host.cpp#L341](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L341), and [nvim_host.cpp#L367](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L367).

- Medium: module boundaries are still blurrier than the repo guide suggests. `AppConfig`’s public header pulls in renderer and text-service types, `draxul-app-support` publicly re-exports heavy dependencies, and tests compile `app/*.cpp` directly into the test binary. That increases rebuild fan-out and makes parallel edits collide more often. See [app_config.h#L7](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/include/draxul/app_config.h#L7), [app_config.h#L101](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/include/draxul/app_config.h#L101), [libs/draxul-app-support/CMakeLists.txt#L24](/Users/cmaughan/dev/Draxul/libs/draxul-app-support/CMakeLists.txt#L24), and [tests/CMakeLists.txt#L54](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt#L54).

- Low: planning metadata has duplicate work items, which will confuse unattended multi-agent work and status tracking. Examples: [20 url-detection-click -feature.md](/Users/cmaughan/dev/Draxul/plans/work-items-icebox/20%20url-detection-click%20-feature.md), [22 url-detection-click -feature.md](/Users/cmaughan/dev/Draxul/plans/work-items-icebox/22%20url-detection-click%20-feature.md), [19 guicursor-full-support -feature.md](/Users/cmaughan/dev/Draxul/plans/work-items-icebox/19%20guicursor-full-support%20-feature.md), [23 guicursor-full-support -feature.md](/Users/cmaughan/dev/Draxul/plans/work-items-icebox/23%20guicursor-full-support%20-feature.md), [16 terminal-host-base-decomposition -refactor.md](/Users/cmaughan/dev/Draxul/plans/work-items-complete/16%20terminal-host-base-decomposition%20-refactor.md), and [17 terminal-host-base-decomposition -refactor.md](/Users/cmaughan/dev/Draxul/plans/work-items-complete/17%20terminal-host-base-decomposition%20-refactor.md).

No builds or tests were run, per instruction. This is a static review from direct file inspection.

## General Review

The codebase is directionally strong. The `libs/` split is real, `app/` is mostly orchestration, test coverage is broad, and the repo has unusually good institutional memory through `plans/` and focused work-item history. The strongest areas are `draxul-grid`, `draxul-app-support` utilities like `CursorBlinker`, the replay fixture support, and the breadth of crash/race/fuzz tests.

The main maintainability risk is concentration, not chaos. A few files remain merge hotspots: [app/app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp), [terminal_host_base.h](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/terminal_host_base.h), [terminal_host_base.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/terminal_host_base.cpp), [terminal_host_base_csi.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/terminal_host_base_csi.cpp), [nvim_host.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp), [vk_renderer.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/vulkan/vk_renderer.cpp), and [metal_renderer.mm](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm). Those are the files most likely to slow parallel agent work.

The test suite is broad but still has seams where behavior is documented rather than exercised. [resize_cascade_tests.cpp#L179](/Users/cmaughan/dev/Draxul/tests/resize_cascade_tests.cpp#L179) explicitly skips the real integration path, [clipboard_tests.cpp#L9](/Users/cmaughan/dev/Draxul/tests/clipboard_tests.cpp#L9) mirrors production code instead of invoking it, and [to-be-checked.md](/Users/cmaughan/dev/Draxul/tests/to-be-checked.md) shows important behaviors still depend on manual verification.

## Top 10 Good Things

1. The repo is genuinely modular instead of only nominally modular.
2. `app/` is mostly orchestration, which matches the stated architecture.
3. Test coverage is broad: unit, integration, fuzz, crash, shutdown-race, and render-reference tests all exist.
4. The render-test fixture/reference setup is practical and maintainable.
5. `tests/support/replay_fixture.h` is a good redraw-bug seam.
6. The work-item history shows the team actually pays down architectural debt.
7. Renderer constants and GPU layout checks are explicit and defensive.
8. Host abstraction is useful and already supports multiple runtime modes cleanly.
9. Cross-platform concerns are mostly pushed below the app layer.
10. The code comments are usually purposeful rather than noisy.

## Top 10 Bad Things

1. Startup rollback can persist config on failure.
2. Public dependency fan-out is still too wide in `draxul-app-support`.
3. Tests bypass boundaries by compiling `app/*.cpp` directly.
4. Some tests mirror production logic instead of calling it.
5. `TerminalHostBase` is still a large collision surface.
6. `app/app.cpp` still centralizes too many coordination concerns.
7. Important behaviors remain manual-checklist-only.
8. Agent helper scripts are visibly duplicated.
9. Planning metadata has duplicate work items and numbering collisions.
10. Some user-facing behavior is still hard-coded rather than configurable.

## Best 10 Quality-of-Life Features To Add

1. Command palette.
2. Live config reload.
3. Configuration UI/editor.
4. Window state persistence.
5. URL detection and click-open.
6. Full `guicursor` support.
7. IME composition visibility/preview.
8. Font fallback inspector.
9. Native tab bar or session tabs.
10. Performance HUD with frame, atlas, and host metrics.

## Best 10 Tests To Add

1. Failed startup must not save or mutate user config.
2. Real clipboard round-trip tests through `NvimHost`, not mirrored helpers.
3. End-to-end resize cascade tests to unskip [resize_cascade_tests.cpp](/Users/cmaughan/dev/Draxul/tests/resize_cascade_tests.cpp).
4. Mouse modifier fidelity tests for button/move/wheel translation.
5. `App::on_display_scale_changed()` failure-path rollback test.
6. Drag-drop/open-file tests with spaces, Unicode, and shell-sensitive characters.
7. Config save/load tests covering partial init and double-shutdown interactions.
8. `UiRequestWorker` tests for stop-while-request-pending and stale resize suppression.
9. Diagnostics-panel-visible render smoke/reference test.
10. Plan/script hygiene tests for duplicated work-item IDs and agent-script CLI consistency.

## Worst 10 Features

1. The Megacity host path still adds conceptual noise to a terminal/editor product.
2. The diagnostics panel is powerful but developer-centric and permanently steals terminal height when shown.
3. Draxul unconditionally installs its own Neovim clipboard provider.
4. The non-Windows emoji-prefixed window title is unnecessary product chrome.
5. Scrollback capacity is fixed instead of user-configurable.
6. Selection limits and truncation policy are still opaque to users.
7. Keybinding customization exists without an in-app discovery/edit surface.
8. File opening is bare `:edit` plumbing, not a polished recent-files/workspace flow.
9. Smooth scrolling exists but lacks tuning and visibility.
10. Close behavior is forceful (`exit` / `:qa!`) rather than graceful and user-aware.