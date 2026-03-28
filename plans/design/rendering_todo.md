# Rendering TODO

## Final Output Color Space

- Megacity currently does lighting in linear space and then applies a manual output gamma step in the final scene shaders before writing to a non-sRGB target.
- The cleaner long-term path is to switch the final presentation target to an actual sRGB framebuffer/attachment format on both Metal and Vulkan.
- When that happens, remove the manual gamma encode from the shader to avoid double-encoding.
- Keep the rule clear:
  - sample authored albedo/base-color textures as sRGB
  - do all lighting and material math in linear
  - let the final sRGB render target perform the linear-to-sRGB conversion
- Audit debug views separately when this changes, because some previews should continue to show raw linear data while presentation/output should be display-encoded.

## Material Branching In The Forward Shader

- The current `material_id` branch in the Megacity forward shader is mostly about surface evaluation, not different lighting models.
- The shared lighting path is the same once the shader has resolved:
  - albedo
  - world-space normal
  - roughness
  - material AO
- The branching is currently needed because Megacity geometry is generic and does not yet provide fully authored per-material UVs/tangents for everything.
- In practice the branch currently decides:
  - which texture set to sample
  - how UVs are generated
  - how the tangent basis/TBN is built
  - how strongly normal/AO maps are applied
- Roads use world-space planar UVs and a simple fixed TBN.
- Buildings currently use dominant-axis mapping because stacked cube geometry does not yet carry authored facade UVs/tangents.
- A future material/mesh cleanup could move more of this into shared material evaluation helpers or mesh-authored data, reducing explicit shader branching.

## Point-Light Shadow Cubemap Sampling

- Megacity directional shadows are now working correctly with cascaded shadow maps, and point-light shadows are also working visually.
- The current point-light receiver path does **not** use direct `samplerCube` lookup for the shadow compare.
- Instead, it:
  - renders the six point-shadow faces as before
  - binds those six faces as six separate 2D textures
  - selects the face explicitly from the dominant axis of `light_to_surface`
  - projects the receiver position with the exact per-face matrix used to render that face
  - samples the selected 2D face directly
- This was introduced because the true cubemap receiver path had cross-backend orientation ambiguity and produced misaligned shadows even when the face renders themselves looked plausible in debug.
- The explicit-face path is a good correctness/debugging baseline, but it has tradeoffs:
  - slightly more receiver-side ALU than `samplerCube`
  - no automatic cubemap seam filtering at face boundaries
  - more shader/descriptors plumbing
- Future cleanup target:
  - revisit true cubemap sampling for point-light shadows
  - validate a canonical face/up-orientation table on both Metal and Vulkan
  - compare the result directly against the current explicit-face path as the ground-truth reference
  - only switch back if the receiver mapping is proven equivalent on both backends
