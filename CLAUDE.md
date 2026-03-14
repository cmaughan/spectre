# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

### Windows
Requires CMake 3.25+, Visual Studio 2022, and Vulkan SDK (with glslc).

```bash
cmake --preset default                              # Configure (Debug, VS 2022 x64)
cmake --preset release                               # Configure (Release)
cmake --build build --config Release --target spectre # Build
```

Run: `.\build\Release\spectre.exe` (requires `nvim` on PATH). Pass `--console` to allocate a debug console window.

### macOS
Requires CMake 3.25+, Xcode Command Line Tools (for Metal compiler).

```bash
cmake --preset mac-debug                             # Configure (Debug)
cmake --preset mac-release                           # Configure (Release)
cmake --build build --target spectre                 # Build
```

Run: `./build/spectre` (requires `nvim` on PATH).

## Project Structure

```
spectre/
├── CMakeLists.txt                  # Top-level: wires libraries + app
├── CLAUDE.md
├── libs/
│   ├── spectre-types/              # Shared POD types (header-only)
│   │   ├── include/spectre/
│   │   │   ├── types.h             # GpuCell, CellUpdate, Color, AtlasRegion, CursorShape
│   │   │   ├── font_metrics.h      # FontMetrics struct
│   │   │   └── events.h            # WindowResizeEvent, KeyEvent, etc.
│   │   └── CMakeLists.txt
│   ├── spectre-window/             # Window abstraction + SDL implementation
│   │   ├── include/spectre/
│   │   │   ├── window.h            # IWindow interface
│   │   │   └── sdl_window.h        # SdlWindow implementation
│   │   ├── src/sdl_window.cpp
│   │   └── CMakeLists.txt
│   ├── spectre-renderer/           # Renderer interface + backends
│   │   ├── include/spectre/
│   │   │   └── renderer.h          # IRenderer interface
│   │   ├── src/vulkan/             # Vulkan backend (Windows)
│   │   ├── src/metal/              # Metal backend (macOS)
│   │   └── CMakeLists.txt
│   ├── spectre-font/               # Font loading, shaping, glyph cache
│   │   ├── include/spectre/
│   │   │   └── font.h              # FontManager, GlyphCache, TextShaper
│   │   ├── src/
│   │   └── CMakeLists.txt
│   ├── spectre-grid/               # 2D cell grid + highlight table
│   │   ├── include/spectre/
│   │   │   └── grid.h              # Grid, Cell, HlAttr, HighlightTable
│   │   ├── src/grid.cpp
│   │   └── CMakeLists.txt
│   └── spectre-nvim/               # Neovim process, RPC, UI events, input
│       ├── include/spectre/
│       │   └── nvim.h              # NvimProcess, NvimRpc, UiEventHandler, NvimInput
│       ├── src/
│       └── CMakeLists.txt
├── app/                            # The executable — just wiring
│   ├── app.h/cpp
│   ├── main.cpp
│   └── renderer_factory.cpp
├── shaders/
└── fonts/
```

## Architecture

Spectre is a Neovim GUI frontend. It spawns `nvim --embed`, communicates via msgpack-RPC over stdin/stdout pipes, and renders the terminal grid using the platform's GPU API (Vulkan on Windows, Metal on macOS).

### Dependency Graph (libraries only link downward)

```
                    spectre-types (header-only)
                   /      |       |       \
          window   renderer   font    grid
            |         |        |       |
            └────┬────┘        └───┬───┘
                 |                 |
              spectre-nvim --------┘
                 |
                app (executable)
```

### Data flow

```
nvim --embed (child process)
  → [msgpack-RPC over pipes, reader thread]
  → NvimRpc notification queue
  → App::run() drains queue each frame
  → UiEventHandler parses ext_linegrid "redraw" events → Grid (2D cell array with dirty tracking)
  → App::update_grid_to_renderer() resolves highlights, shapes text, rasterizes glyphs
  → Renderer buffer write (GpuCell array, 96 bytes/cell)
  → Two-pass instanced draw: background quads, then alpha-blended foreground glyphs
```

### Key abstractions

- **IRenderer** (`libs/spectre-renderer/include/spectre/renderer.h`) and **IWindow** (`libs/spectre-window/include/spectre/window.h`) are abstract interfaces. The renderer knows nothing about fonts, neovim, or text — only colored rectangles and textured quads at grid positions.
- **App** (`app/app.h/cpp`) is the orchestrator that owns all subsystems and runs the main loop.
- Platform-specific renderer implementations live in `libs/spectre-renderer/src/vulkan/` (Windows) and `libs/spectre-renderer/src/metal/` (macOS).

### Rendering

- Buffer indexed by instance index — no vertex buffers, quads generated procedurally in vertex shaders
- Two passes per frame: BG (opaque colored quads) then FG (alpha-blended glyph quads from atlas)
- Host-visible/shared buffer — direct writes, no staging (grid is small)
- Glyph atlas: 2048x2048 R8 texture, shelf-packed, incremental upload
- 2 frames in flight with synchronization primitives
- Pixel format: BGRA8 Unorm (not SRGB — neovim sends colors already in sRGB)

#### Vulkan-specific (Windows)
- SSBO with host-visible coherent memory via VMA
- Descriptor sets, render passes, swapchain management via vk-bootstrap
- Shaders: GLSL 4.50 compiled to SPIR-V via glslc

#### Metal-specific (macOS)
- MTLBuffer with shared storage mode (CPU+GPU visible)
- CAMetalLayer for drawable management
- Shaders: Metal Shading Language compiled to .metallib via xcrun

### Threading

- **Main thread**: SDL events, nvim message processing, grid mutation, GPU rendering
- **Reader thread**: blocking reads from nvim stdout, MPack decode, push to thread-safe queue

All grid and GPU state is only touched by the main thread.

### Neovim RPC

- MPack library with `MPACK_EXTENSIONS=1` (required for neovim's ext types: Buffer/Window/Tabpage)
- Handles `grid_line` run-length encoding, double-width chars, multi-byte UTF-8
- Only renders on `flush` events

### Font pipeline

- FreeType loads face → HarfBuzz shapes codepoints to glyph IDs → GlyphCache rasterizes on-demand with shelf-packing → atlas uploaded to renderer

## Dependencies

All fetched automatically via CMake FetchContent (in `cmake/FetchDependencies.cmake`): SDL3, FreeType, HarfBuzz, MPack. On Windows: vk-bootstrap, VMA. Shaders compiled from GLSL to SPIR-V via glslc (Windows, `cmake/CompileShaders.cmake`) or from Metal to metallib via xcrun (macOS, `cmake/CompileShaders_Metal.cmake`).

## Platform

- **Windows**: MSVC/Visual Studio 2022. Process spawning uses `CreateProcess` with piped stdin/stdout. Built as a Windows GUI app (`WIN32_EXECUTABLE`).
- **macOS**: Clang/Xcode. Process spawning uses `fork()`/`exec()` with `pipe()`. Rendering via Metal.
