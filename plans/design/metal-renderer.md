# Metal Renderer - Cross-Platform Plan

## Phase 1: Platform Abstraction for Process Spawning

**Goal:** Make `NvimProcess` work on macOS.

- Add a POSIX implementation using `fork()`/`execvp()` with `pipe()` for stdin/stdout (alongside the existing `CreateProcess` Windows code)
- Use `#ifdef _WIN32` / `#else` guards in `nvim_process.cpp`
- Update `main.cpp` — the `#else` branch for `main()` already exists but needs the macOS working-directory logic verified

**Files:** `src/nvim/nvim_process.cpp`, `src/main.cpp`

---

## Phase 2: Build System Changes

**Goal:** CMake builds on macOS with either Vulkan or Metal.

- Make `find_package(Vulkan)` conditional on platform (`if(NOT APPLE)`)
- Add Metal framework linking on macOS: `find_library(METAL_FRAMEWORK Metal)`, `QuartzCore`, `Foundation`
- SDL3 already supports macOS natively — no changes needed there
- Add a `cmake/CompileShaders_Metal.cmake` that compiles `.metal` files via `xcrun -sdk macosx metal` -> `.metallib`
- Remove `WIN32_EXECUTABLE` and `ws2_32` from macOS builds
- Conditionally include Vulkan or Metal source files based on platform:
  ```cmake
  if(APPLE)
      file(GLOB_RECURSE METAL_SOURCES src/renderer/metal/*.mm src/renderer/metal/*.h)
      list(APPEND SPECTRE_SOURCES ${METAL_SOURCES})
  else()
      file(GLOB_RECURSE VK_SOURCES src/renderer/vulkan/*.cpp src/renderer/vulkan/*.h)
      list(APPEND SPECTRE_SOURCES ${VK_SOURCES})
  endif()
  ```
- Remove vk-bootstrap and VMA dependencies on macOS

**Files:** `CMakeLists.txt`, `cmake/FetchDependencies.cmake`, new `cmake/CompileShaders_Metal.cmake`

---

## Phase 3: Metal Renderer Implementation

**Goal:** Implement `IRenderer` for Metal. The interface is clean and maps well to Metal.

Create `src/renderer/metal/` with these files:

### `metal_renderer.h/.mm` — implements `IRenderer`

The IRenderer interface maps to Metal concepts:

| IRenderer method | Metal equivalent |
|---|---|
| `initialize(IWindow&)` | Create `MTLDevice`, command queue, get `CAMetalLayer` from SDL window |
| `begin_frame()` | `nextDrawable()`, wait on frame semaphore (triple/double buffer) |
| `end_frame()` | Encode render commands, `presentDrawable`, commit |
| `set_grid_size()` | Resize the Metal buffer (`MTLBuffer`) |
| `update_cells()` | Write directly to `MTLBuffer` contents pointer (shared storage mode) |
| `set_atlas_texture()` | Create `MTLTexture` (R8Unorm, 2048x2048), `replaceRegion` |
| `update_atlas_region()` | `replaceRegion` on existing texture (no staging needed!) |
| `set_cursor()` | Same cursor logic as Vulkan (modify buffer cells) |
| `resize()` | Update `CAMetalLayer` drawable size |
| `set_cell_size()` / `set_ascender()` | Store metrics for push constants equivalent |

### `metal_context.h/.mm` — device and layer setup

- Get `MTLDevice` via `MTLCreateSystemDefaultDevice()`
- Get `CAMetalLayer` from SDL window via `SDL_Metal_GetLayer()` (SDL3 has native Metal support)
- Configure layer: pixel format `.bgra8Unorm`, framebufferOnly = YES

### `metal_pipeline.h/.mm` — render pipelines

- Two `MTLRenderPipelineState` objects (BG and FG), mirroring the Vulkan two-pass approach
- BG pipeline: vertex function generates quads from instance ID, fragment outputs bg color
- FG pipeline: same quad generation, fragment samples atlas texture with alpha blending
- Push constants -> Metal argument buffer or `setVertexBytes` (for 16 bytes of screen/cell size, this is trivial)

### `metal_atlas.h/.mm` — glyph atlas

- `MTLTexture` with `.r8Unorm` format, 2048x2048
- Metal's `replaceRegion` uploads directly from CPU — **no staging buffer needed** (simpler than Vulkan)

### `metal_buffers.h/.mm` — grid buffer

- `MTLBuffer` with `.storageModeShared` — CPU and GPU can both access (equivalent to host-visible coherent in Vulkan)
- Same `GpuCell` layout (96 bytes/cell), same indexing by instance ID
- Metal buffers are simpler: no VMA, no explicit memory allocation flags

---

## Phase 4: Metal Shaders

**Goal:** Port the GLSL shaders to Metal Shading Language.

Create `shaders/` Metal equivalents:

- `grid_bg.metal` — BG vertex + fragment (port from `grid_bg.vert`/`grid_bg.frag`)
- `grid_fg.metal` — FG vertex + fragment (port from `grid_fg.vert`/`grid_fg.frag`)

The translation is straightforward:
- `gl_InstanceIndex` -> `instance_id [[instance_id]]`
- `gl_VertexIndex` -> `vertex_id [[vertex_id]]`
- SSBO -> `device const Cell* cells [[buffer(0)]]`
- Push constants -> `constant PushConstants& pc [[buffer(1)]]`
- Sampler + texture -> `texture2d<float> atlas [[texture(0)]]`, `sampler s [[sampler(0)]]`
- `GpuCell` struct stays identical (same 96-byte layout)

---

## Phase 5: Factory / Conditional Construction

**Goal:** App creates the right renderer for the platform.

In `app.cpp`, conditionally construct the renderer:

```cpp
#if defined(__APPLE__)
    #include "renderer/metal/metal_renderer.h"
    MetalRenderer renderer_;
#else
    #include "renderer/vulkan/vk_renderer.h"
    VkRenderer renderer_;
#endif
```

Or, if you want runtime selection (e.g., for future MoltenVK support on Mac), use a factory function returning `std::unique_ptr<IRenderer>`. But compile-time selection is simpler and sufficient.

The `SdlWindow` needs one small change: on macOS, create the SDL window with `SDL_WINDOW_METAL` flag instead of `SDL_WINDOW_VULKAN`.

---

## Phase 6: macOS Platform Polish

- **App bundle:** Add a `CMakeLists.txt` option to produce a `.app` bundle (`MACOSX_BUNDLE`)
- **Font path:** Ensure the bundled font is copied into `Resources/` inside the bundle
- **Shader path:** Copy `.metallib` into the bundle
- **Retina/HiDPI:** `CAMetalLayer` needs `contentsScale` set to match the window's backing scale factor — SDL3 handles this via `SDL_GetWindowPixelDensity()`

---

## Suggested Implementation Order

1. **Phase 1** (process spawning) — quick win, testable independently
2. **Phase 2** (CMake) — get the build compiling on macOS (with stubs)
3. **Phase 4** (shaders) — port shaders first since they're self-contained
4. **Phase 3** (Metal renderer) — the bulk of the work
5. **Phase 5** (factory wiring) — connect everything
6. **Phase 6** (polish) — app bundle, HiDPI, etc.

## Notes

The IRenderer interface is well-designed for this — it's ~12 methods with no Vulkan leakage. The Metal implementation will be significantly less code than the Vulkan one (Metal doesn't need: VMA, vk-bootstrap, descriptor sets/pools, render passes, framebuffers, pipeline layouts, or staging buffers). The hardest part is getting the initial Metal boilerplate right and ensuring the SSBO-equivalent buffer layout matches the shader expectations exactly.
