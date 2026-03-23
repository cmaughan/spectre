I will begin by listing the contents of the `libs/` and `app/` directories to understand the modular structure and core application logic.
I'll examine `app/main.cpp` and `app/app.cpp` to understand how the application orchestrates its various modules and to identify any initial signs of tight coupling or complexity.
I'll now examine `app/app.cpp` to understand the core application logic and see how the different components are integrated and managed.
I'll now examine the `App` class's members and its main `run` loop to understand how it manages its lifecycle and interacts with the various UI and rendering components.
The `App` class effectively manages its components using dependency injection, which is great for testing. I'll now explore `libs/draxul-nvim` to understand how Neovim is integrated and managed within the application.
The `libs/draxul-nvim` library appears to handle the standard Neovim msgpack-RPC integration. I'll now examine `libs/draxul-grid` and `libs/draxul-renderer`, as they are crucial for the application's rendering performance.
The `libs/draxul-grid` library seems straightforward, likely managing cell-based state. I'll now turn my attention to `libs/draxul-renderer`, which is particularly interesting given its cross-platform Vulkan and Metal support.
The renderer's design, with its common interface and factory, is sound. I'll now examine `libs/draxul-font`, as it's typically a more complex component in Neovim GUIs.
The `libs/draxul-font` stack, with its use of FreeType, HarfBuzz, and glyph caching, is quite advanced. I'll now search the codebase for "TODO", "HACK", and "FIXME" to identify any potential code smells or areas needing attention.
Technical debt seems well-managed, with only a few "TODOs" mostly related to a known `CellText` limitation. I'll now examine the `tests/` directory to evaluate the project's test coverage and see how many tests are currently in place.
With 69 test files, this project appears to have excellent test coverage and a high level of stability. I'll now examine `tests/app_smoke_tests.cpp` or `tests/grid_tests.cpp` to understand the testing style and depth.
The project's use of Catch2 and "Fake" components for CI testing is excellent. I'll now examine the `plans/` directory to understand the project's roadmap and historical progress.
The project's work item naming is professional, reflecting a well-organized workflow. "Worst" things in such a well-run project will likely be minor. I'll now examine `libs/draxul-renderer/src/renderer_state.cpp` to see if the GPU state management introduces any significant complexity.
`RendererState` efficiently manages `GpuCell` structures and dirty regions, though its use of `std::memcpy` and manual byte offsets warrants careful maintenance. I'll now examine `libs/draxul-window/src/sdl_window.cpp`.
The `SdlWindow` implementation's extensive platform-specific DPI and display diagnostics reflect a high-quality, production-ready application. My analysis of the codebase reveals several strengths, including its strict modularity, excellent test coverage, and robust font stack, while also identifying areas for improvement like the large `App` class and manual GPU state management. I'll now examine `split_tree.cpp` and `gui_action_handler.cpp` to further refine my assessment and provide a comprehensive report.
`SplitTree`'s manual recursive tree structure with `std::variant` and `std::unique_ptr` children is a sound approach for window management. I'll now examine `gui_action_handler.cpp`, another important component of the application.
`GuiActionHandler` effectively uses the command pattern to decouple action definitions from `App` logic, reflecting the project's high standards and use of modern C++. I'll now proceed with the final report.# Draxul Codebase Review Report

## Executive Summary
Draxul is a high-performance, cross-platform Neovim GUI built with C++20, utilizing native GPU APIs (Vulkan/Metal). The codebase is exceptionally well-structured, modular, and heavily tested (69 test files). It demonstrates a high level of engineering maturity, with clear separation of concerns, robust dependency injection, and meticulous attention to platform-specific details (DPI, input handling). While some areas like the central `App` class have grown large, the overall technical debt is low and actively managed.

---

## Top 10 Good Things
1.  **Strict Modularity:** Libraries in `libs/` are well-defined with consistent `include`/`src` structures, minimizing circular dependencies.
2.  **Excellent Test Coverage:** 69 test files covering unit tests, end-to-end smoke tests, and RPC fakes. Use of Catch2 and "Fake" components (FakeRenderer, FakeWindow) ensures fast and reliable CI.
3.  **Dependency Injection:** Heavy use of `Deps` structs and factories makes complex components like `App` and `InputDispatcher` highly testable.
4.  **High-Performance Rendering:** Custom Vulkan (Windows) and Metal (macOS) backends provide native GPU performance with a clean `IRenderer` abstraction.
5.  **Robust Font Stack:** Professional text shaping via HarfBuzz and FreeType, combined with a dynamic glyph atlas for low-latency rendering.
6.  **Platform Awareness:** Deep integration with OS-specific features, including high-DPI scaling diagnostics and macOS-specific input overrides (`disable_press_and_hold_macos`).
7.  **Managed Technical Debt:** A very low "TODO" count (only 18 matches) suggests a culture of finishing features and addressing bugs immediately.
8.  **Modern C++ Usage:** Effective use of C++20 features (spans, variants, std::chrono, structured bindings) for safe and expressive code.
9.  **Clear Documentation & Planning:** Detailed `GEMINI.md` for architecture and a structured `plans/` directory for tracking work items.
10. **Startup Performance:** Built-in instrumentation for tracking startup time per component (`time_step` in `App::initialize`).

---

## Top 10 Bad Things
1.  **"God Object" App Class:** `app/app.cpp` is over 750 lines and handles window creation, renderer initialization, host management, and event wiring.
2.  **Hardcoded CellText Limit:** `CellText::kMaxLen = 32` is a known limitation that can cause silent corruption of long grapheme clusters (e.g., complex emojis).
3.  **Manual GPU State Management:** `RendererState` relies on manual byte offsets and `memcpy` for uploading data to the GPU, which is error-prone during refactoring.
4.  **Complex Initialization Sequence:** `App::initialize` is a long, monolithic function that makes error recovery and asynchronous startup difficult.
5.  **Leakage of UI Concerns:** The `IHost` and `I3DHost` interfaces include ImGui-specific methods, tightly coupling the host logic to the ImGui diagnostics panel.
6.  **Shader Duplication:** Platform-specific shaders (Metal vs. GLSL) must be maintained separately, increasing the risk of visual regressions across platforms.
7.  **SDL Dependency in Orchestrator:** While most libraries are abstract, `App` still has direct dependencies on SDL3, making it harder to swap the windowing backend if ever needed.
8.  **Global Log Categories:** Categories are defined in a static enum, preventing new library modules from easily defining their own scoped categories.
9.  **Brittle Smoke Test Timeouts:** Some tests rely on fixed `std::chrono::milliseconds` timeouts, which can lead to flakiness on slow or overloaded CI environments.
10. **Manual Window Activation Hacks:** Multiple platform-specific focus/activation hacks in `sdl_window.cpp` indicate that window lifecycle management is still somewhat fragile.

---

## Top 10 Features to Improve Quality of Life
1.  **Dynamic Font Reloading:** Seamless runtime font switching and fallback configuration without application restart.
2.  **Theme Synchronization:** Automatically sync ImGui and GUI-native colors with the active Neovim colorscheme.
3.  **Built-in Configuration Editor:** A graphical ImGui-based interface to edit and validate `config.toml` in real-time.
4.  **Remote Connection Support:** Connect to remote Neovim instances over TCP or Unix domain sockets.
5.  **Performance Profiling Overlay:** Real-time GPU/CPU frame-time graphs and memory usage statistics.
6.  **Integrated File Explorer:** A fast, native-feeling sidebar for file management that works alongside Neovim.
7.  **Command Palette:** A fuzzy-searchable palette for GUI-specific actions (splits, configuration, font size).
8.  **Lua Scripting for GUI:** Expose an API for users to add custom GUI widgets or automate UI-level behaviors.
9.  **Multi-Window Support:** Allow detaching panes into separate OS windows while maintaining the same Neovim session.
10. **Customizable Hotkeys for GUI:** A robust keybinding system for non-Neovim actions (e.g., toggling panels, zooming).

---

## Top 10 Tests to Improve Stability
1.  **RPC Fuzzing:** Use a fuzzer to send malformed msgpack-RPC packets to ensure the parser never crashes.
2.  **Concurrency Stress Test:** Repeatedly trigger rapid window resizes and RPC traffic to find race conditions.
3.  **GPU Resource Leak Check:** Automated tracking of Vulkan/Metal buffer and texture allocations to ensure 100% cleanup on shutdown.
4.  **DPI Transition Simulation:** Mock DPI changes to verify that the grid and font atlas scale correctly without visual artifacts.
5.  **Unicode Edge-Case Corpus:** A dedicated test suite for complex emojis, ZWJ sequences, and RTL text to stress the `CellText` and HarfBuzz logic.
6.  **Cross-Platform Render Diff:** Compare rendered frames from the same RPC stream between Vulkan and Metal to ensure visual parity.
7.  **Low-Memory Simulation:** Test application behavior when the OS reports low system memory or the GPU runs out of VRAM.
8.  **Input Latency Benchmark:** Automated CI test that measures the time from `KeyEvent` to `RendererState` update.
9.  **Nvim Version Matrix:** Run integration tests against multiple Neovim versions (Stable, Nightly, older releases).
10. **Corrupt Config Recovery:** Test that the app starts with sensible defaults when `config.toml` is missing or contains garbage data.

---

## Worst 10 Features (Existing)
1.  **Inline Cell Storage Cap:** The 32-byte limit on grapheme clusters is the most significant correctness hole.
2.  **ImGui in Host Interface:** Forcing hosts to know about ImGui is a violation of the "Single Responsibility" principle.
3.  **Blocking `App::initialize`:** A single slow initialization step (like font loading or host spawn) freezes the UI entirely.
4.  **Manual Cursor Restoration:** The logic to save and restore the cursor position during cell updates in `RendererState` is brittle.
5.  **Fixed-Size Glyph Atlas:** A static atlas size (though configurable) can be exhausted by very large font sizes or diverse character sets.
6.  **Synchronous RPC Transport:** The underlying RPC transport can block the UI thread if the host (Neovim) becomes unresponsive.
7.  **Hardcoded Smoke Test Deadlines:** Relying on `2000ms` timeouts in `app_smoke_tests.cpp` is a recipe for flakiness.
8.  **Manual Viewport Math:** Scaling and viewport recomputation logic is scattered across `App`, `HostManager`, and `SplitTree`.
9.  **Enum-Based Log Categories:** Limits the extensibility of the logging system for new third-party or internal libraries.
10. **Shader Variant Duplication:** Lack of a unified shader cross-compiler (like SPIRV-Cross) makes maintenance of Vulkan vs. Metal shaders tedious.
