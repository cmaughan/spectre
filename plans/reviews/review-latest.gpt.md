**Scope**
Static review only. I read source from `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, `plans/`, plus [`docs/features.md`](/Users/cmaughan/dev/Draxul/docs/features.md). I did not build, run, or modify anything. `modules/megacity/` implementation was outside the requested scan scope, so MegaCity comments are based on the scanned integration points, docs, tests, and app surface.

**Findings**
1. High: `ToastHost::initialize()` dereferences a possibly-null grid handle. It calls `create_grid_handle()` and immediately uses `handle_` without a null check at [toast_host.cpp:25](/Users/cmaughan/dev/Draxul/app/toast_host.cpp#L25) and [toast_host.cpp:30](/Users/cmaughan/dev/Draxul/app/toast_host.cpp#L30). The same failure path is explicitly guarded in `GridHostBase`, `ChromeHost`, and `CommandPaletteHost`, so this is an outlier and a real crash surface.

2. High: the advertised “thread-safe” toast path does not actually guarantee next-frame delivery. The contract says background threads may call `push()` and the toast will appear “on the next frame” at [toast_host.h:21](/Users/cmaughan/dev/Draxul/app/toast_host.h#L21) and [toast_host.h:28](/Users/cmaughan/dev/Draxul/app/toast_host.h#L28), but `push()` only appends to `pending_` at [toast_host.cpp:10](/Users/cmaughan/dev/Draxul/app/toast_host.cpp#L10), `next_deadline()` ignores pending work at [toast_host.cpp:142](/Users/cmaughan/dev/Draxul/app/toast_host.cpp#L142), and `App::push_toast()` just forwards the message without waking or requesting a frame at [app.cpp:1461](/Users/cmaughan/dev/Draxul/app/app.cpp#L1461). If the app is idle, a background-thread toast can sit invisible until unrelated input arrives.

3. Medium: `ChromeHost` still measures and renders tab/pane labels by byte count in several places, so UTF-8 tab names, pane names, and cwd/status strings will mis-measure, truncate incorrectly, and in pane status rendering may split multibyte sequences into invalid one-byte “clusters.” See byte-based tab sizing at [chrome_host.cpp:288](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp#L288) and [chrome_host.cpp:439](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp#L439), pane-status truncation at [chrome_host.cpp:943](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp#L943), and one-byte glyph warming/rendering at [chrome_host.cpp:1018](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp#L1018). This is now more visible because rename input is UTF-8-aware while layout is not.

4. Medium: glyph-atlas dirty upload ownership is still split across multiple consumers. The core pipeline uploads in [grid_rendering_pipeline.cpp:174](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp#L174) and [grid_rendering_pipeline.cpp:227](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp#L227), while `ChromeHost`, `CommandPaletteHost`, and `ToastHost` each also inspect and clear dirty state at [chrome_host.cpp:1451](/Users/cmaughan/dev/Draxul/app/chrome_host.cpp#L1451), [command_palette_host.cpp:188](/Users/cmaughan/dev/Draxul/app/command_palette_host.cpp#L188), and [toast_host.cpp:175](/Users/cmaughan/dev/Draxul/app/toast_host.cpp#L175). That is a correctness risk around ordering, and it makes every new text overlay a shared-state footgun. This aligns with active WI 109.

5. Medium: the config layer is still not a clean leaf. `draxul-config` publicly links renderer and window at [libs/draxul-config/CMakeLists.txt:15](/Users/cmaughan/dev/Draxul/libs/draxul-config/CMakeLists.txt#L15), and `AppOptions` publicly includes renderer/window types at [app_options.h:9](/Users/cmaughan/dev/Draxul/libs/draxul-config/include/draxul/app_options.h#L9). That means “config” transitively pulls in GPU and windowing APIs, which makes incremental builds heavier and weakens module ownership. This aligns with active WI 110.

6. Medium: the app layer is still the main collaboration bottleneck. `App` owns startup, overlay creation, workspace lifecycle, render-tree assembly, toast routing, diagnostics, layout recomputation, input wiring, and event-loop policy in one class at [app.h:60](/Users/cmaughan/dev/Draxul/app/app.h#L60), [app.cpp:507](/Users/cmaughan/dev/Draxul/app/app.cpp#L507), and [app.cpp:1180](/Users/cmaughan/dev/Draxul/app/app.cpp#L1180). Multiple agents touching overlays, panes, focus, or startup will still collide here.

7. Medium: `InputDispatcher`’s dependency surface is too wide and it repeats similar mouse-routing policy three times. The callback bag at [input_dispatcher.h:51](/Users/cmaughan/dev/Draxul/app/input_dispatcher.h#L51) is effectively an untyped app-service interface, and the same chrome/panel/overlay/host routing pattern is hand-maintained in [input_dispatcher.cpp:303](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L303), [input_dispatcher.cpp:425](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L425), and [input_dispatcher.cpp:490](/Users/cmaughan/dev/Draxul/app/input_dispatcher.cpp#L490). This is exactly the sort of seam that causes drift and merge conflicts.

8. Medium: `NvimHost::dispatch_action()` is still a stringly-typed router mixed with embedded Lua source in the host implementation at [nvim_host.cpp:162](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/nvim_host.cpp#L162). It is workable, but it is harder than necessary to review, unit-test, or split across agents. This aligns with active WI 126 and WI 127.

Some of these are already active work items. The two issues that stood out as current-tree bugs rather than just known refactors were the `ToastHost` null-deref path and the background-thread toast wake/delivery gap.

**Assumptions**
- I did not run tests, so anything about runtime behaviour is inferred from the code paths.
- I filtered out completed and icebox items when judging novelty, but I did reference active work items where the current tree still clearly supports them.
- MegaCity implementation itself was outside the requested scan scope, so MegaCity comments are about the visible maintenance/product footprint, not a fresh deep review of `modules/megacity/`.

**Top 10 Good**
- The repo-level decomposition is real: window, renderer, grid, font, nvim, host, runtime, UI, and config each have a clear home.
- Test coverage breadth is strong for a GUI app: config, RPC, VT, render state, input, startup, shutdown, and many regression cases are already present.
- The fake renderer/window/RPC fixtures make isolated tests practical instead of forcing full app boot.
- `GridHostBase` plus per-host `IGridHandle` is a solid seam for keeping renderer state localized.
- Host/provider registration keeps optional hosts out of most of the core app.
- Render snapshot infrastructure is substantial and gives deterministic visual regression leverage.
- The planning/review workflow is unusually disciplined; the repo has real review memory instead of vague TODOs.
- The config system surfaces unknown keys and parsing problems instead of silently swallowing everything.
- Cross-platform process handling is explicit rather than hidden in preprocessor soup inside the app loop.
- The codebase is willing to extract hard-won regressions into tests, which is why the test tree is valuable rather than decorative.

**Top 10 Bad**
- `App`, `ChromeHost`, and `InputDispatcher` are still the dominant merge-conflict zones.
- Overlay management is hand-wired rather than data-driven.
- Unicode handling is inconsistent in non-terminal UI text paths.
- Toast lifecycle behaviour is under-tested and currently has at least two correctness flaws.
- The config layer still leaks renderer/window dependencies.
- Glyph-atlas upload ownership is split across several subsystems.
- `NvimHost` still mixes transport, action routing, and embedded scripting too tightly.
- Several core files are large enough that review quality will drop unless work stays very focused.
- Important UI surfaces still lack dedicated tests even though the infrastructure to test them exists.
- The application still carries more chrome/control-surface complexity than the core terminal/Nvim workflow really needs.

**Best 10 QoL Features To Add**
- OSC 8 hyperlinks with click-to-open.
- OSC 133 shell marks so users can jump between prompts and copy the last command output.
- Config parse errors with exact line/column and an actionable message.
- Multi-cell ligatures beyond the current two-cell limit.
- Command palette descriptions, aliases, and better discoverability metadata.
- “Open current pane cwd” and “copy current pane cwd” actions from the palette or pane chrome.
- Duplicate focused pane with the same host kind, cwd, and launch options.
- Tab/pane activity badges for shell bell, failed command, or long-running work.
- A smarter paste-confirmation flow with a short preview and explicit size summary.
- Right-click context menus for tabs, pane pills, and status areas so common actions are discoverable without memorising chords.

**Best 10 Tests To Add**
- `ToastHost` background-thread delivery while the app is idle; assert wake/request behaviour.
- `ToastHost` `create_grid_handle()` failure during initialize; assert graceful failure instead of crash.
- `ChromeHost` UTF-8 tab-name width and hit-testing.
- `ChromeHost` UTF-8 pane-status truncation/rendering and rename cases.
- A single-frame atlas-dirty ownership test covering grid, chrome, palette, and toast together.
- `ToastHost` lifecycle coverage for stacking, expiry, fade, and buffered replay.
- App overlay ordering plus input focus handoff across chrome, diagnostics, palette, and toast.
- `ChromeHost` tab-bar geometry across resize and DPI change.
- `CommandPalette` UTF-8 editing, especially backspace over multibyte input.
- `NvimHost` action-dispatch tests that lock down handler registration and emitted Lua payloads.

**Worst 10 Features**
- MegaCity’s large debug/control surface inside the main product.
- MegaCity perf/coverage visualisation modes in a terminal-first app.
- MegaCity sign/tree/material micro-tuning controls.
- The always-visible top tab bar even when the user only has one workspace.
- Inline tab/pane rename before UTF-8 layout is fully hardened.
- The per-pane status strip in its current byte-oriented text implementation.
- Toast notifications in their current wake/lifecycle state.
- The centered half-window command palette with ASCII-centric editing behaviour.
- The manually managed overlay stack as a user-facing capability rather than an internal subsystem.
- The diagnostics panel carrying a lot of runtime-development surface directly into the shipped app.

Overall: the base architecture is much better than a typical cross-platform GUI terminal, and the lower libraries are generally in the right places. The main remaining tax is concentrated in a handful of app-layer coordination files, plus a few overlay/text-path correctness gaps that are now worth fixing before adding more UI surface.