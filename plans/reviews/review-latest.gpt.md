# Draxul Static Review

Static inspection only. I scanned the requested tree under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, `plans/`, plus [`docs/features.md`](/Users/cmaughan/dev/Draxul/docs/features.md). I did not build, run binaries, or execute tests.

## Findings

1. **High: Vulkan grid-handle allocation failure now turns into a null dereference instead of a recoverable init failure.** [`VkRenderer::create_grid_handle()`](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/vulkan/vk_renderer.cpp#L612) can return `nullptr` on descriptor-set allocation failure, but [`GridHostBase::initialize()`](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/grid_host_base.cpp#L14) and [`CommandPaletteHost::dispatch_action()`](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L118) dereference the result unconditionally. One backend resource hiccup becomes a hard crash during host startup or palette open.

2. **High: diagnostics-panel hit-testing is inconsistent on HiDPI because the coordinate contract is split across two places.** [`PanelLayout::contains_panel_point()`](/Users/cmaughan/dev/Draxul/libs/draxul-ui/include/draxul/ui_panel.h#L51) multiplies by `pixel_scale`, while [`InputDispatcher`](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L153) already passes physical coordinates. On Retina/2x displays this double-scales panel hit tests, so mouse routing around the diagnostics panel is wrong.

3. **Medium: “overlay host intercepts all input” is not actually true.** The comment in [`InputDispatcher::on_key_event()`](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L71) says the overlay intercepts all input, but mouse button/move/wheel paths ignore `overlay_host` entirely ([`app/input_dispatcher.cpp:144`](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L144), [`app/input_dispatcher.cpp:168`](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L168), [`app/input_dispatcher.cpp:191`](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L191)), and text-editing events still go straight to the underlying host ([`app/input_dispatcher.cpp:267`](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L267)). That means the command palette can leak wheel/click/IME traffic to the active pane.

4. **Medium: MegaCity can still stall the UI thread by joining worker work synchronously.** [`MegaCityHost::launch_grid_build()`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_host.cpp#L1341) does `grid_thread_.join()` on the caller thread before starting a new build. If the previous grid build is slow, a rebuild path blocks `pump()`/interaction instead of canceling or superseding stale work.

5. **Medium: GUI action definitions have already drifted between runtime dispatch and config persistence.** Runtime actions live in [`GuiActionHandler::action_map()`](/Users/cmaughan/dev/Draxul/app/gui_action_handler.cpp#L21), but persisted/supported config actions are hardcoded separately in [`kKnownGuiActions`](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp#L33). `toggle_megacity_ui` and `edit_config` exist at runtime but not in the serialization list used by [`AppConfig::serialize()`](/Users/cmaughan/dev/Draxul/libs/draxul-config/src/app_config_io.cpp#L481), so custom bindings/unbinds for those actions will not round-trip cleanly.

6. **Medium: library boundaries are good on paper but still porous in practice.** [`draxul-ui`](/Users/cmaughan/dev/Draxul/libs/draxul-ui/CMakeLists.txt#L1) does not declare a renderer dependency, yet [`ui_panel.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-ui/src/ui_panel.cpp#L4) reaches into renderer headers via a relative path. [`draxul-megacity`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/CMakeLists.txt#L80) explicitly includes renderer-private backend directories, and backend-specific files include private render-context headers ([`megacity_render_vk.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_render_vk.cpp#L5), [`megacity_render.mm`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_render.mm#L5)). That makes private renderer refactors expensive and coordination-heavy.

## Architecture Notes

The broad library split is strong: 17 focused `libs/` targets, clear renderer/window/host seams, and a large test corpus. The main collaboration hotspots are still [`app/app.cpp`](/Users/cmaughan/dev/Draxul/app/app.cpp), [`libs/draxul-megacity/src/megacity_host.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_host.cpp), and the backend renderers, which concentrate a lot of responsibility and make concurrent edits riskier than the rest of the tree.

The test suite is broad, but the newest failure surfaces are under-covered: HiDPI panel routing, overlay interception, frame-context ordering, and backend allocation-failure paths.

## Top 10 Good Things

1. The repo is genuinely modular: 17 libraries with mostly clear intent boundaries.
2. The project has an unusually strong test surface for a GUI/renderer codebase: 80 `*_tests.cpp` files.
3. The fake-window/fake-renderer/test-support infrastructure is practical and reused well.
4. `docs/features.md` is current enough to be operationally useful instead of aspirational.
5. The plans/reviews/work-items discipline is excellent and improves multi-agent collaboration.
6. The renderer API split around frame lifecycle, grid handles, and capture support is easy to reason about.
7. Main-thread assertions and explicit reader-thread/main-thread separation show good defensive engineering.
8. Config parsing is careful about types, clamping, and warning on bad user input.
9. Render-test/snapshot infrastructure gives the project a real regression net for visuals.
10. The codebase has many targeted regression tests around bugs that would normally go untested in UI apps.

## Top 10 Bad Things

1. MegaCity complexity dominates too much of the maintenance budget for a terminal/Neovim frontend.
2. Private-header reach-through still weakens the otherwise solid module split.
3. Several new APIs have missing failure-path handling.
4. Input routing mixes logical and physical coordinates in ways that are easy to break.
5. Action definitions are duplicated across runtime, config, menus, docs, and tests.
6. A few files remain too large to parallelize safely without merge friction.
7. UI-thread blocking joins still exist in performance-sensitive paths.
8. Overlay behavior is only partially modeled in the input layer.
9. Some architectural docs and completed work-item narratives no longer match the live code shape.
10. The code is strong on happy paths and regressions, but weaker on degraded-mode behavior and resource exhaustion.

## Best 10 QoL Features To Add

Filtered to avoid features already implemented in [`docs/features.md`](/Users/cmaughan/dev/Draxul/docs/features.md) and items already parked in `work-items-complete/` or `work-items-icebox/`.

1. Pane focus navigation actions (`focus_left/right/up/down`) with default shortcuts.
2. Optional `copy_on_select` behavior for terminal panes.
3. Double-click word selection and triple-click line selection.
4. Right-click context menu for copy/paste/open-file/split/close actions.
5. Command-palette MRU sorting so recent actions rise to the top.
6. “Open recent file/directory” command-palette source backed by drop/open history.
7. Optional focus-follows-mouse for split panes.
8. User-defined command-palette aliases/macros for common multi-step actions.
9. Open selected filesystem paths with the system opener when the selection resolves to a path.
10. Quick theme/font profile switching without editing `config.toml`.

## Best 10 Tests To Add

1. HiDPI unit test for [`PanelLayout::contains_panel_point()`](/Users/cmaughan/dev/Draxul/libs/draxul-ui/include/draxul/ui_panel.h#L51) at `pixel_scale=2`.
2. Input-routing test that a visible diagnostics panel blocks host mouse input correctly on HiDPI.
3. Overlay-routing test that an active command palette blocks underlying mouse button events.
4. Overlay-routing test that an active command palette blocks wheel events.
5. Overlay-routing test that `TextEditingEvent` goes to the overlay instead of the host.
6. Host-init test for `create_grid_handle()==nullptr` in [`GridHostBase`](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/grid_host_base.cpp#L14).
7. Palette test for `create_grid_handle()==nullptr` in [`CommandPaletteHost`](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L118).
8. Config round-trip test for custom `toggle_megacity_ui` bindings.
9. Config round-trip test for unbinding `toggle_megacity_ui`.
10. Renderer frame-context ordering test that records grid, render-pass, diagnostics ImGui, and palette draws in the expected order.

## Worst 10 Features

1. **MegaCity host**: impressive technically, but it is still the least maintainable and least core-to-purpose feature in the product.
2. **Split panes opening the platform default shell**: this breaks user intent when the active pane is `nvim` or another non-shell host.
3. **Diagnostics panel as a bottom dock only**: it consumes precious terminal height and has no export/share workflow.
4. **Command palette**: useful, but too flat; it lacks recency, categories, and argument discoverability.
5. **Selection with an 8192-cell cap**: practical but surprisingly limiting for a terminal frontend.
6. **Mouse-only terminal selection workflow**: still awkward for keyboard-heavy users.
7. **Open-file dialog integration**: present, but unbound by default and still routed through a stringly `open_file:` action contract.
8. **MegaCity UI toggle persistence**: currently vulnerable to config round-trip drift.
9. **Diagnostics-panel mouse routing on HiDPI**: current behavior is fragile enough to count as a weak feature experience.
10. **Command-palette overlay input model**: currently leaky enough that it does not feel like a fully modal overlay.

## Bottom Line

Draxul’s foundations are much better than average for a cross-platform GUI frontend: modular layout, real tests, solid planning hygiene, and good regression discipline. The main current risks are not “lack of architecture”, but contract drift and edge-path correctness in the newest refactors: allocation failure, HiDPI routing, overlay input semantics, and boundary leakage between libraries.

If you want the next cleanup wave to pay off fastest, I would fix the two crash/input bugs first, then tighten the action registry and renderer/UI module boundaries.