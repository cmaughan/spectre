# Draxul Geometry Plan

## Goal

Add a new static library, `draxul-geometry`, owned below Megacity, whose job is to generate procedural geometry.

The first generator is `DraxulTree`: a bark/trunk/branch mesh generator that outputs:

- positions
- normals
- colors
- tangents
- texture coordinates
- triangle indices

This library should stay renderer-agnostic. It generates CPU mesh data only. Megacity consumes that data later.

## Why A Separate Library

`draxul-megacity` already has `mesh_library.cpp`, but that file is currently a fixed-shape mesh helper for:

- cubes
- signs
- road quads
- grids

Tree generation is a different problem:

- recursive procedural structure
- tweakable generation parameters
- deterministic randomness
- future threading
- future reuse outside Megacity

That belongs in a dedicated library rather than as more helpers inside Megacity.

## Proposed Library Boundary

New library:

- `libs/draxul-geometry`

Suggested shape:

- `libs/draxul-geometry/CMakeLists.txt`
- `libs/draxul-geometry/include/draxul/geometry_mesh.h`
- `libs/draxul-geometry/include/draxul/tree_generator.h`
- `libs/draxul-geometry/src/tree_generator.cpp`
- `libs/draxul-geometry/src/tree_generator_internal.h`

Suggested dependencies:

- `glm`
- `draxul-types` only if we want shared POD helpers or logging

Non-dependencies:

- no renderer headers
- no Megacity headers
- no app headers

Megacity should link to `draxul-geometry`, not the other way around.

## Mesh Format

The geometry vertex format should become the shared mesh format for Megacity, replacing the duplicate private `SceneVertex`/`MeshData` pair over time.

The compatibility constraint is important:

- current Megacity pipelines already consume:
  - position
  - normal
  - color
  - uv
  - `tex_blend`
- we want to add tangent data without blowing up the current renderer in one step

So the shared vertex should preserve those first five attributes and append tangent data:

```cpp
struct GeometryVertex
{
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 color{1.0f};
    glm::vec2 uv{0.0f};
    float tex_blend = 0.0f;
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f}; // xyz + handedness
};

struct GeometryMesh
{
    std::vector<GeometryVertex> vertices;
    std::vector<uint32_t> indices;
};
```

Reason:

- bark normal mapping wants a real tangent basis
- tree meshes may grow beyond `uint16_t` indices
- Megacity can consume the shared format incrementally because the existing first five attributes stay stable

## First Public API

```cpp
struct TreeAgeProfile
{
    float age_years = 8.0f;
};

struct DraxulTreeParams
{
    uint64_t seed = 1;

    float overall_scale = 1.0f;
    float max_height = 18.0f;
    float max_canopy_radius = 7.0f;

    int radial_segments = 10;

    float trunk_length = 5.0f;
    float trunk_base_radius = 0.35f;
    float trunk_tip_radius = 0.18f;

    float branch_length_scale = 0.72f;
    float branch_radius_scale = 0.68f;
    int max_branch_depth = 3;

    int child_branches_min = 1;
    int child_branches_max = 3;

    float branch_spawn_start = 0.25f;
    float branch_spawn_end = 0.9f;

    float upward_bias = 0.35f;
    float outward_bias = 0.75f;
    float droop_bias = 0.05f;

    float curvature = 0.12f;
    float taper_power = 1.25f;
    float tip_ring_spacing_scale = 0.5f;

    glm::vec3 bark_color_root{0.22f, 0.15f, 0.10f};
    glm::vec3 bark_color_tip{0.42f, 0.30f, 0.20f};
    float bark_color_noise = 0.08f;
};

[[nodiscard]] DraxulTreeParams make_tree_params_from_age(float age_years);
[[nodiscard]] GeometryMesh generate_draxul_tree(const DraxulTreeParams& params);
```

The `make_tree_params_from_age()` helper gives a useful high-level control immediately:

- sapling
- young tree
- mature tree
- grand tree

Age is a high-level dial. The rest remain tweakable.

## High-Level Generation Strategy

The core primitive is a branch built from rings.

Each branch is generated as:

1. create a base ring
2. create another ring above it
3. sew the two rings into triangles
4. keep adding rings until the branch reaches its target length
5. taper radius toward the tip
6. reduce ring spacing near the tip
7. optionally cap the tip

This is the correct starting model. It matches the shape language you described and is much easier to reason about than trying to grow arbitrary triangles directly.

## Internal Branch Representation

The important internal structure is not a half-edge mesh first. It is a branch skeleton plus explicit ring records.

Suggested internal types:

```cpp
struct BranchDescriptor
{
    int depth = 0;
    int parent_branch = -1;
    int parent_ring = -1;
    int parent_ring_center_index = -1;

    float length = 1.0f;
    float radius_start = 0.2f;
    float radius_end = 0.05f;

    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    glm::vec3 frame_up{1.0f, 0.0f, 0.0f};
};

struct RingRecord
{
    glm::vec3 center{0.0f};
    glm::vec3 tangent{0.0f, 1.0f, 0.0f};
    glm::vec3 normal{1.0f, 0.0f, 0.0f};
    glm::vec3 bitangent{0.0f, 0.0f, 1.0f};
    float radius = 0.1f;
    float distance_along_branch = 0.0f;
    std::vector<uint32_t> vertex_indices;
};
```

That gives enough information to:

- build the branch surface
- compute UVs
- attach child branches locally
- generate tangents consistently

## Why Not Start With Half-Edge

A full half-edge structure is probably unnecessary for v1.

The hard problem here is not generic mesh editing. It is:

- building a coherent recursive branch skeleton
- generating rings in stable local frames
- attaching child branches cleanly to parent branches

A full half-edge mesh adds complexity early:

- more memory
- more invariants
- harder debugging
- more threading friction

For the first version, use:

- branch descriptors
- per-branch ring arrays
- explicit local stitching data for parent/child connections

If later branch unions need more sculpting freedom, add a smaller local adjacency layer first. Only move to half-edge if branch junction editing truly demands it.

## Branch Shape

Each branch needs:

- base radius
- tip radius
- total length
- radial segment count
- ring spacing
- curve direction / curvature

The branch centerline should not be perfectly straight for all branches. Even v1 should support a slight curve.

Recommended rule:

- trunk: mostly vertical, mild noise/lean only
- child branches: oriented from parent tangent plus upward/outward bias and some random deviation

Ring spacing should shrink as radius shrinks.

Simple default:

```text
spacing = lerp(base_spacing, base_spacing * tip_ring_spacing_scale, taper_t)
```

where `taper_t` runs from branch root to tip.

That gives denser rings toward the tip automatically.

## Branch Attachment Strategy

This is the hard part, but it is still manageable without a general-purpose topology library.

### Parent Side Selection

For a child branch:

1. choose a parent ring index along the parent branch
2. choose a center angle on that ring
3. choose an attachment span, for example 3 to 5 vertices wide

That gives a local patch on the parent branch surface.

### Child Root Frame

Build the child’s initial frame from:

- parent ring tangent
- outward radial direction from the chosen center angle
- upward bias

Then normalize and construct a child TBN frame.

### Stitching

Do not try to boolean-union a child branch into the parent.

Instead:

1. create a slightly elliptical child root ring
2. anchor it to the chosen parent surface patch
3. bridge the patch boundary to the child root with triangles
4. allow some overlap or burying of the child root inside the parent branch if needed

This is the right compromise for v1:

- robust
- visually plausible
- debuggable

The root should be slightly smaller than the parent surface patch so the connection reads as growth rather than as two pipes intersecting.

## Recursive Growth

Branch generation order:

1. build trunk descriptor
2. build trunk rings
3. choose child branch spawn points on the trunk
4. generate child descriptors
5. recurse until:
   - `max_branch_depth`
   - minimum radius threshold
   - minimum length threshold

Each child branch should scale relative to its parent:

- `child_length = parent_length * branch_length_scale * random_range(...)`
- `child_radius = parent_radius * branch_radius_scale * random_range(...)`

Branch spawn count should come from a range:

- `child_branches_min`
- `child_branches_max`

with deterministic random sampling from the seed.

## Normals, Tangents, UVs, Colors

### Normals

Branch body normals should be radial from the branch centerline.

At junctions:

- v1 can use the generated body normals plus a small smoothing blend near the branch root
- later versions can do an angle-weighted recompute over the final mesh

### Tangents

Tangents should follow the bark texture direction:

- tangent points around or along the branch consistently
- bitangent is derived from normal + handedness

For bark materials, the most useful convention is:

- `U` wraps around the branch circumference
- `V` runs along branch length

Then the tangent should align with increasing `U` or `V` consistently across the library. Pick one convention and keep it fixed.

Recommended:

- `U` = around circumference
- `V` = along length
- tangent = circumference direction

### UVs

For each ring:

- `U = i / radial_segments`
- `V = accumulated_length / bark_tile_world_length`

Child branches should continue their own local `V`, not try to maintain one giant global unwrap initially.

### Colors

Per-vertex color should be a bark gradient:

- darker near trunk/root
- lighter toward branch tips
- mild seeded noise

This gives immediate usable color even before a bark texture exists.

## Defaults And High-Level Controls

The key high-level control should be `age`.

Age should not mean “infinite growth.” It should map into bounded generation defaults.

Suggested bands:

- `0.5 - 2 years`: sapling
- `3 - 8 years`: young tree
- `9 - 20 years`: mature tree
- `20+ years`: grand tree, but still capped by `max_height` and `max_canopy_radius`

Defaults should keep trees believable and bounded.

Future presets can expose:

- spindly tree
- mature tree
- dense bush
- ornamental tree

For now, start with:

- one good “young to mature” default
- age-based helper

## Determinism And Randomness

Tree generation should be deterministic for a given seed and parameter set.

That is important for:

- tests
- threaded generation
- config-driven regeneration
- stable visual identity

Use hash-derived RNG streams per branch instead of one mutable global RNG when possible.

Suggested rule:

- trunk uses `seed`
- each child branch uses `hash(seed, parent_branch_id, child_slot)`

That makes recursion deterministic and thread-friendly.

## Threading Strategy

Threading matters, but not everywhere.

The clean first split is:

1. descriptor generation
2. mesh emission

### Descriptor Generation

Generate the branch tree first:

- branch descriptors only
- parent/child relationships
- no final vertex writes yet

This stage is light and deterministic.

### Mesh Emission

Once descriptors are fixed:

- emit each branch body in parallel into per-branch temporary meshes
- emit branch junction bridges in a second pass
- merge the temporary meshes into one final `GeometryMesh`

This is a much safer threading model than trying to mutate one shared mesh during recursive growth.

Recommended v1:

- single-threaded first
- keep the data layout ready for parallel branch emission

Recommended v2:

- parallelize only branches at depth >= 1 or sibling branches

## Megacity Integration Constraint

Megacity’s current mesh path is still mostly fixed-mesh and instanced by `MeshId`.

That works for:

- cube
- sign
- road quad
- grid

It does not naturally fit unique procedural tree meshes.

So there are two integration options:

### Option A: Prototype Tree Mesh

Generate one tree mesh per parameter set and instance it many times.

Pros:

- lowest renderer churn
- fast to ship

Cons:

- limited variation per tree

### Option B: Dynamic Mesh Registration

Add a procedural mesh upload/cache layer to Megacity:

- procedural mesh key
- uploaded GPU mesh cache
- `SceneObject` references cached procedural mesh handles instead of only `MeshId`

Pros:

- supports unique trees
- future-proofs geometry generation

Cons:

- more renderer work now

Recommendation:

- v1 tree generation library can exist independently of this choice
- Megacity integration should probably start with Option A
- if trees become a serious feature, move to Option B

## Phased Implementation Plan

### Phase 1: Library Skeleton

- add `libs/draxul-geometry`
- add `GeometryVertex` / `GeometryMesh`
- add unit tests for ring emission and normals/uv continuity
- no Megacity integration yet

### Phase 2: Trunk Generator

- generate one tapered trunk branch only
- caps at root/tip as needed
- output normals, tangents, uvs, colors

Done when:

- a trunk renders cleanly
- bark UVs are stable
- tangents are coherent

### Phase 3: Recursive Branch Generator

- add child branch descriptors
- add parent-ring-based attachment
- add stitched root bridges
- seeded randomness

Done when:

- the result reads as a tree rather than a pipe cluster

### Phase 4: Age + Presets

- age helper
- bounds/caps for height and canopy
- sensible defaults for sapling to grand tree

### Phase 5: Megacity Integration

- add initial `Tree` geometry usage path
- either prototype mesh instancing or procedural mesh cache
- replace placeholder cube trees

### Phase 6: Quality Pass

- junction smoothing improvements
- bark materials
- optional leaves / leaf cards
- LOD and/or simplified distant trees
- threaded sibling branch emission

## Testing Plan

Add geometry-focused tests, not renderer-only tests.

Examples:

- trunk generator emits expected vertex/index counts for a fixed segment/ring count
- branch radii decrease monotonically toward the tip
- UV `V` increases monotonically along the branch
- generated normals are unit length
- generated tangents are orthogonal to normals within tolerance
- same seed + params produces byte-identical geometry
- different seeds produce different geometry

Megacity-side integration tests can come later.

## Recommended First Defaults

Start simple:

- radial segments: `10`
- trunk length: `5.0`
- trunk base radius: `0.35`
- trunk tip radius: `0.18`
- max branch depth: `3`
- child branches per branch: `1..3`
- branch length scale: `0.72`
- branch radius scale: `0.68`
- upward bias: `0.35`
- outward bias: `0.75`
- curvature: `0.12`

That should produce something tree-like without immediately exploding into dense canopy complexity.

## Recommendation

The right first deliverable is:

- `draxul-geometry` library
- standalone trunk generator
- branch descriptor system
- ring-based child branch attachments
- deterministic seed-driven output

Do not begin with:

- half-edge
- leaves
- wind
- full LOD system
- renderer threading tricks

The core problem is branch skeleton + ring stitching. Solve that cleanly first.
