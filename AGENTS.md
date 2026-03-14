# Spectre Agent Guide

## Scope

This repository is a cross-platform Neovim GUI frontend with:

- Vulkan rendering on Windows
- Metal rendering on macOS
- SDL3 windowing/input
- msgpack-RPC communication with `nvim --embed`

The codebase is intentionally split into small libraries. Keep app code thin and push platform or subsystem logic downward into `libs/`.

## Build And Test

### Windows

Requirements:

- CMake 3.25+
- Visual Studio 2022
- Vulkan SDK with `glslc`
- `nvim` on `PATH` for runtime checks

Commands:

```powershell
cmake --preset default
cmake --build build --config Release --parallel
ctest --test-dir build --build-config Release --output-on-failure
```

Run:

```powershell
.\build\Release\spectre.exe
.\build\Release\spectre.exe --console
```

### macOS

Requirements:

- CMake 3.25+
- Xcode Command Line Tools
- `nvim` on `PATH` for runtime checks

Commands:

```bash
cmake --preset mac-debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run:

```bash
./build/spectre
```

## Architecture

- `libs/spectre-types`: shared POD types and event structs
- `libs/spectre-window`: `IWindow` abstraction and SDL implementation
- `libs/spectre-renderer`: `IRenderer` abstraction, backend factory, Vulkan and Metal backends
- `libs/spectre-font`: FreeType/HarfBuzz font loading, shaping, glyph atlas management
- `libs/spectre-grid`: terminal grid model, dirty tracking, highlights
- `libs/spectre-nvim`: embedded Neovim process, RPC transport, redraw parsing, input translation
- `app/`: orchestration only

Preferred dependency direction:

- `app` depends on public headers only
- renderer backends stay private to `spectre-renderer`
- pure logic stays testable without launching Neovim or opening a window

## Validation Expectations

- If you touch RPC, redraw handling, or input translation, run `ctest`.
- If you touch renderer code, build the platform-specific app target and verify startup at least once.
- If you change build wiring, keep both Windows and macOS paths valid in CI.

## Replay Fixtures

Use `tests/support/replay_fixture.h` for redraw-oriented tests. It provides small builders for:

- msgpack-like arrays and maps
- `grid_line` cell batches
- full `redraw` event vectors

This is the preferred way to reproduce UI parsing bugs without launching Neovim.

## Review Consensus

When the user asks to "come to consensus" on reviews, do not just concatenate or summarize review files.

Treat it as a synthesis task:

- read the review notes from the relevant agent folders under `plans/reviews/`
- identify where the agents agree, where one review adds useful detail, and where there is real disagreement or just a sequencing difference
- reconcile the review notes against the current tree so already-fixed or stale issues are called out instead of repeated blindly
- produce a planning-oriented consensus note with suggested fix order, not just a findings list
- where helpful, explicitly attribute points to the agent models that raised them

The result should read like a conversation and planning review for fixes, with a current recommended path forward.

## Known Pitfalls

- Do not include backend-private renderer headers from `app/`.
- Keep shutdown paths non-blocking; a stuck Neovim child must not hang the UI on exit.
- Font-size changes must relayout existing grid geometry even before Neovim acknowledges a resize.
- Unicode rendering is still cell-oriented. Be careful when changing shaping or grid-line parsing because combining clusters and wide glyphs are easy to regress.
