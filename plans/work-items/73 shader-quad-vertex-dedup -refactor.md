---
# WI 73 — Shader Quad Vertex Offset Array Deduplication

**Type:** refactor  
**Priority:** medium (highest structural divergence risk in shaders)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

The 6-vertex quad offset table (two triangles covering a unit rectangle) is hardcoded in four separate shader files:
- `shaders/grid_bg.vert` (GLSL, Vulkan)
- `shaders/grid_fg.vert` (GLSL, Vulkan)
- Metal background vertex shader (in `grid.metal`)
- Metal foreground vertex shader (in `grid.metal`)

Any change (e.g. winding order, UV flip, NDC adjustment) must be applied to all four. Similarly, `PushConstants` / uniform layout is duplicated across the GLSL shaders and Metal.

`decoration_constants_shared.h` already demonstrates the pattern: a header that works in both GLSL and Metal. The quad offset table and push constants should follow the same pattern.

---

## Investigation Steps

- [ ] Read `shaders/grid_bg.vert`, `shaders/grid_fg.vert`, `shaders/grid.metal`
- [ ] Confirm the quad offset table is identical in all four; note any differences
- [ ] Read `decoration_constants_shared.h` to understand how shared GLSL/Metal includes work
- [ ] Check if the Vulkan GLSL preprocessor and Metal preprocessor have compatible `#include` mechanisms (xcrun metallib uses its own include path; glslc uses `-I` flags)

---

## Proposed Design

Create `shaders/quad_offsets_shared.h`:
```glsl
// Works in both GLSL 4.50 and Metal Shading Language
const vec2 kQuadOffsets[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);
```

Then in each shader: `#include "quad_offsets_shared.h"`.

Also evaluate whether `PushConstants` / uniform struct can be shared similarly.

---

## Implementation

- [ ] Create `shaders/quad_offsets_shared.h` with the canonical offset table
- [ ] Update `CMakeLists.txt` glslc invocations to pass `-I shaders/` so `#include` works
- [ ] Update `CMakeLists_Metal.cmake` to pass `-I shaders/` to xcrun
- [ ] Replace hardcoded tables in all four shaders with `#include "quad_offsets_shared.h"`
- [ ] Build both platforms; run render tests to verify visual output is unchanged

---

## Acceptance Criteria

- [ ] Single definition of the quad offset table
- [ ] All render tests pass (no winding order or UV regression)
- [ ] Both Vulkan (Windows) and Metal (macOS) shaders compile successfully

---

## Notes

CI may need shader include path updates. If the Metal compiler does not support `#include` from arbitrary paths cleanly, fall back to a separate `quad_offsets_metal.h` with the same content — still better than 4 independent copies.
