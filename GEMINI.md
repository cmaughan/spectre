# GEMINI.md - Draxul Project Context

## Project Overview
Draxul is a high-performance, cross-platform Neovim GUI frontend built with C++20. It leverages native GPU rendering (Vulkan on Windows, Metal on macOS) for low-latency text and grid updates.

- **Architecture:** Highly modular, organized into internal libraries (`libs/`) covering types, windowing (SDL3), rendering (Vulkan/Metal), font management (FreeType/HarfBuzz), grid state, and Neovim RPC.
- **Key Technologies:**
  - **Windowing:** SDL3
  - **GPU APIs:** Vulkan (Windows), Metal (macOS)
  - **Text Stack:** FreeType, HarfBuzz, Dynamic Glyph Atlas
  - **Communication:** msgpack-RPC via `nvim --embed`
- **Main Goal:** Provide a visually polished and responsive Neovim experience with robust Unicode/ligature support and native OS integration.

## Building and Running

### Prerequisites
- **Windows:** CMake 3.25+, Visual Studio 2022, Vulkan SDK (with `glslc`), `nvim` on PATH.
- **macOS:** CMake 3.25+, Xcode CLT, `nvim` on PATH.

### Build Commands
The project uses CMake Presets for configuration.

- **Windows (Debug):**
  ```powershell
  cmake --preset default
  cmake --build build --config Debug --parallel
  ```
- **macOS (Debug):**
  ```bash
  cmake --preset mac-debug
  cmake --build build --parallel
  ```

### Running the App
- **Windows:** `.\build\Debug\draxul.exe` (or `.\r.bat` for a quick build & run)
- **macOS:** `./build/draxul` (or `./r.sh`)
- **Flags:** `--console` (Windows only, opens log console), `--smoke-test` (runs a brief initialization check).

### Convenience Scripts
- `r.bat` / `r.sh`: Build and run the application.
- `t.bat` / `t.sh`: Build and run the test suite.

## Testing
Draxul includes a suite of native tests covering grid logic, RPC parsing, input handling, and renderer state.

- **Run Tests:** Use `t.bat` (Windows) or `./t.sh` (macOS).
- **Underlying Tool:** CTest is used as the test runner.
- **Integration Test:** Includes a `draxul-app-smoke` test that verifies the app can spawn Neovim and reach a "flushed" UI state within a timeout.

## Development Conventions

### Architecture and Modularity
- **Encapsulation:** The renderer backend (Vulkan/Metal) is hidden behind the `IRenderer` interface. App-level code should only interact with `IRenderer.h`.
- **Libraries:**
  - `draxul-types`: Shared PODs, events, and logging.
  - `draxul-window`: SDL3 window abstraction.
  - `draxul-renderer`: Public API and platform-specific backends.
  - `draxul-font`: Font loading, shaping, and glyph caching.
  - `draxul-grid`: Cell-based UI state and highlights.
  - `draxul-nvim`: Process management and msgpack-RPC protocol.

### Coding Style
- **Standard:** C++20.
- **Naming:** `snake_case` for variables and functions, `PascalCase` for classes and enums.
- **Logging:** Use the `DRAXUL_LOG_<LEVEL>(category, ...)` macros.
  - **Categories:** `App`, `Rpc`, `Nvim`, `Window`, `Font`, `Renderer`, `Input`, `Test`.
  - **Levels:** `Error`, `Warn`, `Info`, `Debug`, `Trace`.
- **Assets:** Shaders and fonts are copied to the build output directory during the post-build step.

### Git Workflow
- Always verify changes against the `draxul-app-smoke` test if `nvim` is available.
- Do not commit generated build artifacts or large log files.
- Adhere to the `.clang-format` configuration for code formatting.

## Key Files
- `app/main.cpp`: Entry point, handles platform-specific initialization.
- `app/app.cpp`: Main application orchestrator.
- `libs/draxul-renderer/include/draxul/renderer.h`: Public rendering interface.
- `libs/draxul-nvim/src/ui_events.cpp`: Processes Neovim UI redraw events.
- `CMakeLists.txt`: Root build configuration.
- `cmake/FetchDependencies.cmake`: External library management.
