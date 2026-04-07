Static-only review from direct reads of `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, `plans/`, and [docs/features.md](/Users/cmaughan/dev/Draxul/docs/features.md). I did not build, run, edit, or rely on pre-generated combined files. I also excluded items already parked in `plans/work-items-complete/` and `plans/work-items-icebox/`, and I did not repeat stale review findings that are already fixed in the current tree.

**Findings**
1. High: [LocalTerminalHost::pump()](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/local_terminal_host.cpp#L134) still drains output until empty with no wall-clock budget. Under bursty PTY output, one host can monopolize the main thread and starve input/render.
2. Medium-High: `draxul-config` is still not a clean low-level config module. It publicly links renderer/window in [libs/draxul-config/CMakeLists.txt](/Users/cmaughan/dev/Draxul/libs/draxul-config/CMakeLists.txt#L15), the facade header re-exports runtime options in [app_config.h](/Users/cmaughan/dev/Draxul/libs/draxul-config/include/draxul/app_config.h#L3), and [app_options.h](/Users/cmaughan/dev/Draxul/libs/draxul-config/include/draxul/app_options.h#L3) directly includes renderer/window headers. That defeats dependency direction and widens rebuild and merge surface.
3. Medium-High: glyph-atlas dirty/upload ownership is duplicated across [grid_rendering_pipeline.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp#L227), [chrome_host.cpp](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp#L439), [command_palette_host.cpp](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L188), and [toast_host.cpp](/Users/cmaughan/dev/Draxul/app/toast_host.cpp#L175). Multiple subsystems both upload and clear dirty state, which is fragile and hard to reason about in multi-host frames.
4. Medium: [NvimHost::pump()](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L101) polls `window().clipboard_text()` every frame on the main thread. It is thread-safe, but it is still eager OS polling in a hot path for every Neovim pane.
5. Medium: [NvimHost::dispatch_action()](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L216) contains the same `open_file_at_type:` handler twice, including duplicated embedded Lua blocks at [L216](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L216) and [L248](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L248). That is a concrete maintenance bug magnet.
6. Medium: [App::dispatch_to_nvim_host()](/Users/cmaughan/dev/Draxul/app/app.cpp#L1294) finds “the nvim host” by checking `host.debug_state().name == "nvim"`. That is a debug-string heuristic, not a capability boundary.
7. Medium: overlay hosts are hardwired throughout [app/app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp). `chrome_host_`, `palette_host_`, `toast_host_`, and `diagnostics_host_` are each threaded through init, render-tree assembly, viewport updates, diagnostics, and shutdown at [L323](/Users/cmaughan/dev/Draxul/app/app.cpp#L323), [L1079](/Users/cmaughan/dev/Draxul/app/app.cpp#L1079), [L1220](/Users/cmaughan/dev/Draxul/app/app.cpp#L1220), and [L1386](/Users/cmaughan/dev/Draxul/app/app.cpp#L1386). Every new overlay now means broad app-layer edits and merge conflicts.
8. Medium: `HostManager` is not fully generic. [create_host_for_leaf()](/Users/cmaughan/dev/Draxul/app/host_manager.cpp#L397) still special-cases `MegaCityHost` via `dynamic_cast` at [L430](/Users/cmaughan/dev/Draxul/app/host_manager.cpp#L430), while complexity is concentrated in very large files like [app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp), [megacity_host.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_host.cpp), [megacity_render_vk.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_render_vk.cpp), [vk_renderer.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/vulkan/vk_renderer.cpp), [metal_renderer.mm](/Users/cmaughan/dev/Draxul/libs/draxul-renderer/src/metal/metal_renderer.mm), and [megacity_scene_tests.cpp](/Users/cmaughan/dev/Draxul/tests/megacity_scene_tests.cpp). That is the biggest “hard for multiple agents” risk in the tree.

Overall: the repo structure is better than average, but app-layer orchestration and MegaCity-specific complexity are slowly re-centralizing knowledge that the library split was supposed to keep separated.

**Top 10 Good**
- The `draxul-*` library split is real and mostly coherent.
- The host and renderer hierarchies are conceptually clean.
- Test coverage is broad, not token.
- [tests/support/replay_fixture.h](/Users/cmaughan/dev/Draxul/tests/support/replay_fixture.h) is the right abstraction for redraw/parser regressions.
- [docs/features.md](/Users/cmaughan/dev/Draxul/docs/features.md) is unusually complete and useful.
- The config and CLI surface is explicit and discoverable.
- Render snapshot infrastructure exists and is documented.
- Input routing and keybinding behavior have strong dedicated coverage.
- The tree shows evidence of fixing previous review findings instead of letting them rot.
- The app still keeps a lot of pure logic testable without spawning Neovim or a real window.

**Top 10 Bad**
- [app/app.cpp](/Users/cmaughan/dev/Draxul/app/app.cpp) is too large and too central.
- MegaCity consumes a disproportionate amount of complexity for a Neovim GUI frontend.
- Overlay management is not data-driven.
- Config types still leak runtime/renderer concerns.
- Atlas upload ownership is ambiguous.
- Capability checks sometimes fall back to debug strings and concrete casts.
- Large embedded Lua strings in host code are hard to test and review.
- Several source files are too large for comfortable parallel work.
- Some hot paths still do eager polling or unbounded draining on the main thread.
- The codebase’s strongest modularity story weakens at the app and MegaCity boundaries.

**Best 10 QoL Features To Add**
- Rename workspace tabs.
- Reopen the last closed pane or tab.
- Move a pane to a new or existing tab.
- Duplicate the current pane with the same host and working directory.
- Saved layout presets such as “editor + terminal” and “three-column”.
- A keybinding inspector that shows which action matched and why.
- Clipboard history with a paste picker.
- A temporary focus mode that hides chrome, toasts, and diagnostics.
- Recent files and recent working-directories switcher in the command palette.
- Send selected text or the current command directly to another pane.

**Best 10 Tests To Add**
- A fairness test proving [LocalTerminalHost::pump()](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/local_terminal_host.cpp#L134) cannot starve the frame loop under repeated drains.
- A regression test that `open_file_at_type:` in [nvim_host.cpp](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L216) emits exactly one RPC action.
- A `ChromeHost` test for tab-bar hit-testing and viewport updates across resize and DPI changes.
- A `ToastHost` lifecycle test for stacking, expiry, fade, and `request_frame()` behavior.
- An `App` render-tree test covering overlay ordering with diagnostics, palette, toast, and chrome on/off.
- A workspace-tab test for focus preservation when switching, closing, and reopening mixed-host tabs.
- A mixed-host dispatch test proving app actions target the intended Neovim pane without debug-name heuristics.
- A file-drop test for spaces, unicode, and shell-sensitive paths across host kinds.
- A `UiRequestWorker` test for overlapping requests, ordering, and cancellation semantics.
- A command-palette overlay test covering window resize plus diagnostics-height changes in one flow.

**Worst 10 Features**
These are the lowest-ROI current features, not necessarily broken ones.

- MegaCity point-shadow face and stored-depth debug views.
- MegaCity point-shadow depth-delta view.
- MegaCity tangent, bitangent, and packed-TBN debug views.
- MegaCity tree micro-tuning controls.
- MegaCity sign sizing controls.
- MegaCity sign color styling controls.
- MegaCity building shading micro-knobs like `Middle Strip Push` and `Alternate Darken`.
- MegaCity hex/oct threshold tuning.
- MegaCity projection toggle.
- MegaCity `Perf` / `Coverage` / `LCOV Coverage` / `Perf Log Scale` control surface as a shipping app feature rather than a separate dev tool.

If you want a follow-up, the highest-leverage next step is to turn findings 1, 2, 3, and 7 into a small refactor plan: they are the best maintainability wins without needing a product decision.