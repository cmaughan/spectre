I will begin by invoking the `codebase_investigator` to obtain a high-level architectural overview and identify key symbols and dependencies across the project's modules.
I will manually list the contents of the `libs/` directory to understand the internal library structure and then examine key files in `app/` and `libs/` to assess modularity and code quality.
I will examine `app/app.h` and `app/app.cpp` to evaluate their structure, and then I will review the `libs/draxul-renderer` interface and implementation.
I'll check `libs/draxul-renderer/include/draxul/renderer.h` to see how it abstracts the rendering, and then I'll examine the Neovim interaction in `libs/draxul-nvim`.
The renderer abstraction is well-structured with its use of interfaces and the `RendererBundle`. Now, I'll examine the Neovim RPC and UI headers, and I'll also verify the grid library's interface.
The codebase is well-structured and consistently named, though `App` appears quite complex. I'll now examine `libs/draxul-grid/include/draxul/grid.h` to understand grid state management and check the `shaders/` directory for their organization.
I'll examine `libs/draxul-font` for font loading and shaping, and `libs/draxul-types` for common types.
I'll examine `libs/draxul-font/include/draxul/text_service.h` for font and glyph management, and `app/main.cpp` for the overall system wiring.
The codebase is exceptionally modular and clean. I'll now examine `libs/draxul-app-support/include/draxul/app_config.h` for configuration handling and `libs/draxul-window/include/draxul/sdl_window.h` for window management.
The use of interfaces like `IWindow` and `IGridSink` is a great design choice, and the C++20 code is very clean. I'll now investigate `libs/draxul-megacity`, as the name implies a complex 3D or UI component.
`MegaCity` appears to be a host for 3D content, possibly UI-integrated, which further demonstrates the project's robust modularity. I'll now examine `plans/` for the roadmap and work items to identify any known issues or areas slated for improvement.
I'll re-examine the `plans/` directory; the initial tree indicated several subdirectories, including `work-items/`, `work-items-complete/`, and `work-items-icebox/`. Since `list_directory` reported `work-items/` as empty, I'll check the other two and the root of `plans/`.
I'll check `plans/work-items-complete/`.
The extensive list of completed work items highlights a strong focus on stability, testing, and refactoring. I'll now examine `plans/design/` to understand the architectural vision.
I'll read `plans/design/learnings.md` and `plans/design/belt-and-braces.md`.
The design documents are very helpful. I'll now examine the `scripts/` directory to see the available utility scripts.
`export_all_code.py` looks like a handy tool for feeding the entire codebase into an LLM. I'll also take a look at `do.py` in the root directory.
The project's tooling for testing, rendering, and AI code reviews is remarkably sophisticated. I'll now examine `plans/prompts/review.md` to see the criteria used by the AI reviewers.
Meta! `plans/prompts/review.md` is nearly identical to my own prompt, so the project effectively has a built-in reviewer. I'll check `plans/reviews/` to see past findings and identify any lingering issues.
I'll read `plans/reviews/review-consensus.md`.
The previous reviews highlighted issues with `MegaCity`, `TerminalHostBase`, the RPC queue, `App.cpp`, and the `AppConfig`/`SDL3` coupling. I'll now examine the current code, starting with `libs/draxul-host/src/terminal_host_base.cpp`, to see if these have been resolved, especially since `IRenderer` still includes `I3DPassProvider` for `MegaCity`.
`TerminalHostBase` has been successfully decomposed, significantly reducing its size and complexity, with CSI handling moved to its own file. I'll now examine `libs/draxul-nvim/src/rpc.cpp` to see how the RPC notification queue is handled.
I'll check `NvimRpc::drain_notifications` in `libs/draxul-nvim/src/rpc.cpp`.
The notification queue remains a simple `std::vector` swapped under a mutex, and the bounded queue/coalescing mentioned in "Belt and Braces" hasn't been implemented yet. I'll check `plans/work-items-icebox/` to see if it's currently deferred.
I've got a solid handle on the project: it's a high-quality C++ codebase with strong testing and modularity, but there's room to improve by removing `MegaCity`, further decomposing `TerminalHostBase`, decoupling `AppConfig` from `SDL3`, hardening the RPC queue, and shrinking the `App` class. I'll now investigate "bad code smells" and review `libs/draxul-renderer/src/vulkan/vk_renderer.cpp` to gauge rendering complexity, especially since `MegaCity` was flagged as a major architectural misstep.
The Vulkan renderer's use of VMA is a solid choice, and the project's overall health looks excellent. I'll now examine `tests/render/` to review the render test scenarios.
I'll read `tests/render/basic-view.toml`.
The render test system is impressive, providing robust cross-platform UI validation by capturing frames from `nvim` sessions. I've gathered enough to report: the project boasts excellent modularity, strong testing, and sophisticated tooling, but it's held back by `MegaCity`'s impact on the renderer abstraction, the complexity of `TerminalHostBase` and `App`, and an unbounded RPC queue. I've identified several QoL features and stability tests to prioritize, alongside technical debts like synchronous RPC blocks and tight coupling in `AppConfig`. I'll now briefly check `libs/draxul-ui` before finalizing.
I'll read `libs/draxul-ui/include/draxul/ui_panel.h`.
`UiPanel` is a well-encapsulated ImGui implementation, and with that final look, I'm ready to present my comprehensive architectural report on Draxul. It's a high-quality C++ project with standout modularity and testing, though it's currently encumbered by technical debts like the `MegaCity` integration and an unbounded RPC queue. I'll structure the final report as requested, detailing the top 10 good and bad aspects, 10 QoL features, 10 stability-improving tests, and the 10 worst features/debts.

# Draxul Architectural Review Report

## Executive Summary
Draxul is a high-performance Neovim frontend with an exceptionally clean and modular architecture. The project demonstrates a strong commitment to testing and developer tooling, which is rare for a GUI application of this scale. While the core is robust, there are areas of "feature creep" (like MegaCity) and technical debt (unbounded queues) that should be addressed to maintain long-term stability and maintainability.

---

## Top 10 Good Things
1.  **Exceptional Modularity:** Libraries like `draxul-font`, `draxul-grid`, and `draxul-renderer` are well-separated with clear, stable interfaces.
2.  **Strong Testing Culture:** The project includes unit tests, integration tests, and a powerful deterministic render-test system that ensures UI consistency.
3.  **Modern C++20 Adoption:** Clean usage of modern C++ features (variants, spans, chrono, and concepts) keeps the code expressive and safe.
4.  **Renderer Abstraction:** The platform-specific backends (Metal/Vulkan) are cleanly hidden behind `IRenderer`, allowing for easy extension.
5.  **Sophisticated CI/Tooling:** The `do.py` script and AI-integrated code review process significantly lower the barrier for high-quality contributions.
6.  **Deterministic Render Tests:** The `.toml`-based system for capturing and comparing frames from Neovim sessions is a premier feature for cross-platform validation.
7.  **Transparent Design Process:** The `plans/` directory documents design decisions, "learnings," and a clear roadmap, providing excellent context for contributors.
8.  **High-Performance Text Stack:** The combination of `TextService` and a dynamic `GlyphAtlas` ensures efficient rendering of complex Unicode and ligatures.
9.  **Robust RPC Handling:** The separation of msgpack-RPC logic into its own library (`draxul-nvim`) makes it easier to test and reason about Neovim communication.
10. **Developer Hygiene:** Mandatory formatting via `pre-commit` and clear work-item tracking in the repository maintain a high bar for code quality.

## Top 10 Bad Things
1.  **Renderer Interface "Leak":** `I3DPassProvider` (introduced for MegaCity) is a specialized hack that leaks platform-specific handles into the core renderer interface.
2.  **Unbounded RPC Queue:** The notification queue in `NvimRpc` uses a simple `std::vector`, posing a risk of memory bloat or high latency under heavy load.
3.  **`TerminalHostBase` Complexity:** Even with recent decompositions, the CSI handling in `terminal_host_base_csi.cpp` remains a massive, hard-to-maintain file.
4.  **`App` God-Class Tendencies:** `App` coordinates too many high-level concerns, making it a potential bottleneck for future structural changes.
5.  **Tight Configuration Coupling:** `AppConfig` is somewhat coupled to `SDL3` and the filesystem, which hinders headless or unit testing of configuration logic.
6.  **Inconsistent Error Reporting:** The strategy for error propagation varies across modules (return bool, log directly, or store an error string), which can lead to missed failures.
7.  **Manual Instrumentation:** `StartupStep` tracking in `App::initialize` is manual and brittle, easily falling out of sync with actual initialization steps.
8.  **Synchronous RPC Risk:** While assertions exist, the architecture still permits synchronous calls to Neovim that can block the UI thread and degrade performance.
9.  **Hardcoded Styles:** The ANSI color palette and certain buffer sizes are hardcoded, limiting user customization without code changes.
10. **Platform Fragmentation:** Some cross-platform logic is handled via `#ifdef` blocks in `App.cpp` rather than through cleaner abstractions or strategy patterns.

---

## Best 10 QoL Features to Add
1.  **Live Configuration Reloading:** Automatically apply changes to `config.json` without requiring an app restart.
2.  **Performance HUD:** A toggleable overlay showing real-time FPS, frame timings, and RPC round-trip latency.
3.  **Command Palette:** A `Ctrl+Shift+P` interface for GUI-specific actions (e.g., toggling transparency, changing fonts).
4.  **Font Fallback Inspector:** A UI tool to visualize which fonts are providing which glyphs, helping users debug their font setup.
5.  **Remote Neovim Attach:** Support for connecting to an existing Neovim instance over a socket or pipe.
6.  **User-Defined ANSI Palettes:** Allow users to define their own terminal colors and themes via JSON.
7.  **IME Composition Visibility:** Better visual feedback and placement for IME input within the grid.
8.  **Native Tab Bar Integration:** Leverage OS-native tabs for a more integrated feel on macOS and Windows.
9.  **Integrated Documentation Browser:** View Draxul and Neovim documentation directly within the app.
10. **Smooth Scrolling:** Interpolated scrolling for a modern, fluid experience during large buffer jumps.

## Best 10 Tests to Improve Stability
1.  **DPI Hotplug Stress Test:** Rapidly switching display scales to ensure resource recreation is thread-safe and robust.
2.  **RPC Burst/Backlog Test:** Simulating massive notification bursts to verify queue bounding and latency recovery.
3.  **Grid Mid-Frame Resize Test:** Stress-testing the renderer's ability to handle resizing while frame updates are in-flight.
4.  **Startup Rollback Integrity:** Ensuring all OS resources (processes, pipes, window handles) are cleaned up on various init failures.
5.  **Multi-Monitor Coordinate Test:** Validating mouse and window coordinate math across monitors with different DPI scales.
6.  **VT Parser Fuzzing:** Extensive fuzzing of the VT sequence parser to prevent crashes on malicious or malformed input.
7.  **Clipboard Sync Stress Test:** Verifying bidirectional clipboard integrity under heavy load with large multi-byte payloads.
8.  **Alt-Screen Restore Consistency:** Ensuring the primary grid state is perfectly preserved and restored after an alt-screen session.
9.  **Mpack Codec Roundtrip Fuzz:** Fuzzing the msgpack encoder/decoder with deep nesting and invalid types.
10. **Shutdown Race Stress Test:** Forcing shutdowns while the RPC reader thread is under heavy load to catch cleanup races.

## Worst 10 Features (Technical Debt)
1.  **MegaCity 3D Pass:** The `I3DPassProvider` hack that breaks the general renderer abstraction.
2.  **Unbounded Notification Vector:** The simple, unbounded `std::vector` in `NvimRpc` for notifications.
3.  **Synchronous RPC Blocking:** Potentially blocking calls to Neovim on the main thread.
4.  **Monolithic CSI Parser:** The overly large `terminal_host_base_csi.cpp` implementation file.
5.  **Renderer RTTI/Casting:** Using `dynamic_cast` for feature detection in the `RendererBundle`.
6.  **Manual Startup Timing:** Brittle, manual instrumentation of initialization steps.
7.  **Hardcoded ANSI Palette:** Lack of user-customizable terminal color schemes.
8.  **Coupled `AppConfig`:** Coupling of configuration logic to `SDL3` and specific filesystem paths.
9.  **Manual Vulkan Readback Buffer:** Manual lifecycle management of capture buffers in `VkRenderer`.
10. **Global Thread Identity:** Using a global variable for main-thread assertions in `NvimRpc`.
