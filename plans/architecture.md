# Architecture Plan: Library Separation for Agent-Driven Development

## Principles

1. **Each library has one CLAUDE.md** — an agent dropped into any library directory has everything it needs to understand, build, and test that library in isolation.
2. **Interfaces are headers-only, in a shared `include/` tree** — no library links to another library's implementation, only its interface header.
3. **Data flows through plain structs** — shared types are POD/trivial structs in a `spectre-types` header lib. No smart pointers, no vtables, no templates crossing boundaries.
4. **One CMakeLists.txt per library** — each is independently configurable and testable. The top-level CMake just wires them together.
5. **Agents work on one library at a time** — clear inputs, clear outputs, clear tests. An agent modifying `spectre-font` never needs to read renderer code.

## Proposed Library Structure

```
spectre/
├── CMakeLists.txt                  # Top-level: wires libraries + app
├── CLAUDE.md                       # Project-wide context
│
├── libs/
│   ├── spectre-types/              # Shared POD types (header-only)
│   │   ├── CMakeLists.txt
│   │   ├── CLAUDE.md
│   │   └── include/spectre/
│   │       ├── types.h             # GpuCell, CellUpdate, Color, AtlasRegion, CursorShape
│   │       ├── font_metrics.h      # FontMetrics struct
│   │       └── events.h            # WindowResizeEvent, KeyEvent, etc.
│   │
│   ├── spectre-window/             # Window abstraction + SDL implementation
│   │   ├── CMakeLists.txt
│   │   ├── CLAUDE.md
│   │   ├── include/spectre/
│   │   │   └── window.h            # IWindow interface
│   │   └── src/
│   │       ├── sdl_window.h
│   │       └── sdl_window.cpp
│   │
│   ├── spectre-renderer/           # Renderer interface + backends
│   │   ├── CMakeLists.txt
│   │   ├── CLAUDE.md
│   │   ├── include/spectre/
│   │   │   └── renderer.h          # IRenderer interface
│   │   ├── vulkan/                  # Vulkan backend (compiled on Windows)
│   │   │   ├── vk_renderer.h/cpp
│   │   │   ├── vk_context.h/cpp
│   │   │   ├── vk_pipeline.h/cpp
│   │   │   ├── vk_atlas.h/cpp
│   │   │   └── vk_buffers.h/cpp
│   │   └── metal/                   # Metal backend (compiled on macOS)
│   │       ├── metal_renderer.h
│   │       └── metal_renderer.mm
│   │
│   ├── spectre-font/               # Font loading, shaping, glyph cache
│   │   ├── CMakeLists.txt
│   │   ├── CLAUDE.md
│   │   ├── include/spectre/
│   │   │   └── font.h              # Public API: FontManager, GlyphCache, TextShaper
│   │   └── src/
│   │       ├── font_manager.cpp
│   │       ├── glyph_cache.cpp
│   │       └── text_shaper.cpp
│   │
│   ├── spectre-grid/               # 2D cell grid + highlight table
│   │   ├── CMakeLists.txt
│   │   ├── CLAUDE.md
│   │   ├── include/spectre/
│   │   │   └── grid.h              # Public API: Grid, HighlightTable
│   │   └── src/
│   │       └── grid.cpp
│   │
│   └── spectre-nvim/               # Neovim process, RPC, UI events, input
│       ├── CMakeLists.txt
│       ├── CLAUDE.md
│       ├── include/spectre/
│       │   └── nvim.h              # Public API: NvimProcess, NvimRpc, UiEventHandler, NvimInput
│       └── src/
│           ├── nvim_process.cpp
│           ├── rpc.cpp
│           ├── ui_events.cpp
│           └── input.cpp
│
├── app/                            # The executable — just wiring
│   ├── CMakeLists.txt
│   ├── CLAUDE.md
│   ├── app.h
│   ├── app.cpp
│   └── main.cpp
│
├── shaders/
└── fonts/
```

## Dependency Graph (libraries only link downward)

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

Rules:
- **spectre-types**: no dependencies. Pure POD structs.
- **spectre-window**: depends on spectre-types. Links SDL3.
- **spectre-renderer**: depends on spectre-types + spectre-window (IWindow interface). Links Vulkan/Metal.
- **spectre-font**: depends on spectre-types. Links FreeType + HarfBuzz.
- **spectre-grid**: depends on spectre-types.
- **spectre-nvim**: depends on spectre-types + spectre-grid + spectre-window (for input events).
- **app**: depends on everything. Thin orchestrator.

## Why This Structure Works for AI Agents

### 1. Bounded context per agent
An agent working on `spectre-font` only needs:
- `spectre-font/CLAUDE.md` (what this library does, how to build/test it)
- `spectre-types/include/` (the shared types it consumes/produces)
- Nothing else

### 2. Interface-driven contracts
The `include/spectre/*.h` headers are the contract. An agent implementing a new renderer backend reads `renderer.h` and implements it. It never sees grid code, font code, or nvim code.

### 3. Independent testing
Each library can have a `tests/` directory with standalone test binaries:
```
libs/spectre-font/tests/
    test_glyph_cache.cpp    # Rasterize glyphs, verify atlas packing
    test_shaper.cpp         # Shape known strings, verify glyph IDs
```
An agent can run `cmake --build build --target spectre-font-tests` without building the full app.

### 4. Parallel agent work
These tasks can be done simultaneously by different agents with zero conflicts:
- Agent A: Add italic/bold font support in `spectre-font`
- Agent B: Add WebGPU renderer backend in `spectre-renderer`
- Agent C: Add multigrid support in `spectre-nvim` + `spectre-grid`
- Agent D: Add smooth scrolling in `app`

### 5. CLAUDE.md per library
Each library's CLAUDE.md contains:
- **Purpose**: one paragraph
- **Public API**: the interface header, key types
- **Build**: how to build and test this library alone
- **Internals**: brief description of implementation approach
- **Constraints**: what NOT to do (e.g., "never include Vulkan headers from the public interface")

## Key Refactoring Needed

### Break the renderer/types.h dependency
Currently `renderer/types.h` contains `GpuCell`, `CellUpdate`, `Color`, `AtlasRegion`, `CursorShape` — these are used by grid, font, and nvim. Move them to `spectre-types`. The renderer interface references them but doesn't own them.

### Decouple nvim/ui_events from grid
Currently `ui_events.h` directly includes `grid/grid.h` and mutates the grid. Instead, `UiEventHandler` should produce events/commands that the app applies to the grid. This breaks the nvim→grid link and makes both independently testable.

### Decouple nvim/input from window
Currently `input.h` includes `window/window.h` for event types. Move event types (`KeyEvent`, `MouseButtonEvent`, etc.) to `spectre-types/events.h`.

### Factory for renderer creation
The app currently uses `#ifdef __APPLE__` to pick the renderer. Instead, provide a factory function in `spectre-renderer`:
```cpp
std::unique_ptr<IRenderer> create_renderer(IWindow& window);
```
This hides the platform selection inside the library.

## Migration Path

1. Create `libs/spectre-types/` — extract shared structs. All existing code switches to `#include <spectre/types.h>`.
2. Create `libs/spectre-window/` — move window code, expose IWindow.
3. Create `libs/spectre-renderer/` — move renderer code, expose IRenderer.
4. Create `libs/spectre-font/` — move font code.
5. Create `libs/spectre-grid/` — move grid code.
6. Create `libs/spectre-nvim/` — move nvim code, decouple from grid.
7. Move `app.*` and `main.cpp` to `app/`.
8. Add per-library CLAUDE.md files.
9. Add per-library test targets.

Each step is a single commit that builds and runs. No big-bang refactor.
