# Repository Review

Static inspection only. I read the current tree under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/`, and checked recent history up to commit `5fae90d` / `c48c6a9`. I did not build, run, or edit anything.

## Findings

1. High: [`app/main.cpp#L127`](/Users/cmaughan/dev/Draxul/app/main.cpp#L127) changes the process working directory to the executable directory. That is a global side effect which leaks into config lookup, relative asset paths, subprocesses, tests, and future agent work. It makes behavior depend on launch style rather than explicit path handling.
2. High: [`app/host_manager.cpp#L207`](/Users/cmaughan/dev/Draxul/app/host_manager.cpp#L207) still does two-phase host capability wiring and RTTI-based dispatch via `dynamic_cast<I3DHost*>`. That makes host initialization less declarative, harder to reason about, and awkward for parallel changes because capability registration is split across `IHost`, `I3DHost`, `HostContext`, and `HostManager`.
3. High: [`app/app.cpp#L61`](/Users/cmaughan/dev/Draxul/app/app.cpp#L61) and [`app/app.cpp#L521`](/Users/cmaughan/dev/Draxul/app/app.cpp#L521) show `App` still owns too many concerns at once: startup rollback, config load/save, window lifecycle, renderer orchestration, input wiring, host pumping, diagnostics, DPI handling, and shutdown policy. The class is better than a monolith used to be, but it is still the main merge-conflict hotspot.
4. Medium: [`libs/draxul-window/src/sdl_event_translator.cpp#L32`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_event_translator.cpp#L32) intentionally samples mouse modifiers from `SDL_GetModState()`. The comments are honest, but the boundary is still lossy. This is exactly the kind of platform quirk that regresses later because the approximation is centralized but not modeled explicitly in tests.
5. Medium: [`app/macos_menu.mm#L18`](/Users/cmaughan/dev/Draxul/app/macos_menu.mm#L18) stores menu actions in global mutable state. That makes lifecycle/ownership implicit, complicates multi-window futures, and is difficult to test cleanly.
6. Medium: [`libs/draxul-grid/include/draxul/grid.h#L136`](/Users/cmaughan/dev/Draxul/libs/draxul-grid/include/draxul/grid.h#L136) still truncates clusters beyond 32 bytes. The code warns, and tests cover truncation well, but it remains a product-level correctness compromise that will keep leaking into Unicode, ligature, and clipboard behavior.
7. Medium: `libs/draxul-app-support` is no longer a cohesive module. It contains config I/O, keybindings, render-test parsing, grid rendering glue, cursor blinking, and UI request threading. The library name now means “misc runtime glue”, which is a sign the architecture needs another split.
8. Medium: [`libs/draxul-ui/src/ui_panel.cpp#L25`](/Users/cmaughan/dev/Draxul/libs/draxul-ui/src/ui_panel.cpp#L25) and [`libs/draxul-ui/src/ui_panel.cpp#L186`](/Users/cmaughan/dev/Draxul/libs/draxul-ui/src/ui_panel.cpp#L186) show the diagnostics panel owns a separate ImGui context plus dock-builder layout plus direct input feeding. It works, but it is a fairly invasive subsystem for a debug panel and adds hidden state transitions that make UI bugs harder to isolate.
9. Medium: [`scripts/ask_agent_gpt.py#L11`](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gpt.py#L11), [`scripts/ask_agent_claude.py#L11`](/Users/cmaughan/dev/Draxul/scripts/ask_agent_claude.py#L11), and [`scripts/ask_agent_gemini.py#L11`](/Users/cmaughan/dev/Draxul/scripts/ask_agent_gemini.py#L11) duplicate path resolution, prompt loading, prepend handling, and subprocess scaffolding. This is already acknowledged in planning, and it is real drift risk.
10. Medium: test coverage is strong in core logic, but there are visible holes around `main`/CLI behavior, SDL translation boundaries, native file dialog bridging, native clipboard wrappers, renderer/host factory seams, and macOS menu integration. Those are exactly the places where platform regressions tend to survive static refactors.

## Overall Assessment

The codebase is in better shape than many GUI frontends at this stage. The library split is real, the public/private header boundary is mostly respected, and the test suite is far deeper than average in risky areas like VT parsing, redraw replay, backpressure, DPI, and startup/shutdown rollback.

The main maintainability problem is no longer “everything is in one file”; it is “the remaining hot paths are orchestration-heavy and capability-heavy”. `App`, `HostManager`, the terminal host base, and the SDL/UI boundary still attract too much coordination logic, which will slow multiple agents down and keep reintroducing subtle integration bugs.

## Top 10 Good Things

1. The module breakdown is mostly sane, with clear subsystem intent across `types`, `grid`, `font`, `renderer`, `window`, `nvim`, `host`, and `ui`.
2. The project enforces a real public/private header boundary instead of casually including backend internals from `app/`.
3. The test suite is unusually thorough for a native GUI app, especially around VT parsing, redraw replay, DPI, shutdown races, and config recovery.
4. `SplitTree` is a focused, testable layout unit rather than pane logic being smeared through the app loop.
5. `RendererBundle` and the renderer capability interfaces are a meaningful improvement over one giant renderer type.
6. `GridRenderingPipeline` is a good example of pushing policy below the app layer.
7. The planning/work-item history is rich and maps cleanly to the code; it is useful engineering memory, not dead paperwork.
8. Startup rollback and failure-path testing are taken seriously instead of being left to manual QA.
9. Many hard limits are bounded explicitly: VT parser buffers, RPC queue depth, selection limits, atlas size, etc.
10. The repository has strong reviewability because tests and plans are named by behavior, not by vague ticket numbers.

## Top 10 Bad Things

1. `App` is still the dominant hotspot for lifecycle, rendering, input, and persistence policy.
2. `HostManager` still knows too much about host capabilities and host construction policy.
3. The SDL boundary is still partly approximate instead of being a fully explicit translation layer.
4. `draxul-app-support` has become a miscellaneous bucket.
5. The macOS menu implementation is singleton-style and not lifecycle-friendly.
6. The Unicode cell model still relies on truncation and special-case repair.
7. A few large files remain difficult to modify in parallel: `app/app.cpp`, `terminal_host_base.cpp`, `terminal_host_base_csi.cpp`, `vk_renderer.cpp`, `metal_renderer.mm`.
8. Native/platform seams are less tested than pure logic seams.
9. Script tooling has visible duplication and will drift.
10. The diagnostics panel stack is heavier and more stateful than a debug tool should be.

## Best 10 Quality-of-Life Features To Add

1. Live config reload.
2. Searchable scrollback.
3. Command palette.
4. URL detection and click-to-open.
5. Window state persistence.
6. Native tab bar.
7. IME composition visibility and positioning polish.
8. Per-pane environment and working-directory overrides.
9. Font fallback inspector in the diagnostics UI.
10. User-facing performance HUD with frame, atlas, RPC, and host metrics.

## Best 10 Tests To Add

1. Direct unit tests for `sdl_event_translator.cpp`, especially mouse modifier edge cases and display-scale events.
2. CLI tests for `app/main.cpp` argument parsing and process working-directory behavior.
3. Tests around `renderer_factory.cpp` and `host_factory.cpp` so capability wiring changes fail loudly.
4. Native file dialog marshalling tests for `sdl_file_dialog.cpp`.
5. Clipboard wrapper tests for `sdl_clipboard.cpp`.
6. Multi-pane diagnostics tests proving the panel reports the intended host state.
7. macOS menu action wiring tests at the boundary of `macos_menu.mm`.
8. Nvim clipboard-provider failure tests around `nvim_get_api_info`, `clipboard_set`, and request shutdown races.
9. Text-input-area / IME tests across split panes and DPI changes.
10. Long-run integration tests for repeated split/close/reopen cycles with mixed host kinds.

## Worst 10 Features

1. Process-wide current-directory mutation on startup.
2. RTTI-based post-init host capability attachment.
3. Global mouse modifier sampling at the SDL boundary.
4. Global singleton state in the macOS menu layer.
5. The debug panel’s separate ImGui-context stack for a non-core feature.
6. String-encoded `open_file:` action dispatch instead of a typed command path.
7. Hard 32-byte cell-text truncation as a first-class rendering behavior.
8. Clipboard provider injection via embedded Lua strings in `NvimHost`.
9. Title-bar color updates coupled directly to grid flush state.
10. The optional MegaCity path, which still adds conceptual weight despite being peripheral.

## Bottom Line

This is a serious codebase with better structure and test discipline than most projects in its category. The next quality jump is not more features first; it is reducing orchestration density in `App`/`HostManager`, tightening native boundary tests, and removing the remaining implicit global behaviors. That would make the repository materially easier for multiple agents to change safely.