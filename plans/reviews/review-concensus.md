# Review Consensus — Spectre
**Sources:** `claude-sonnet-4-6`, `gpt-5`
**Intent:** planning conversation for fixes, not a raw dump of findings

---

## Shared Diagnosis

Both agents agree on the same high-level picture:

- the top-level module split is good
- the repo is navigable and structurally sane
- the main risk is not "bad code everywhere"
- the real problem is that a few important seams still collapse too many concerns together

The strongest areas of agreement are:

1. `App` is still too large and too policy-heavy
2. the code is weakest at module seams, not at isolated local logic
3. the Neovim/RPC path needs a stronger contract
4. tests exist and are useful, but they stop short of the hardest cross-module behaviour

---

## Where Both Agents Agree Strongly

### 1. `App` is the main conflict hotspot

**claude-sonnet-4-6:** called out `App` as the primary blocker for parallel work and proposed extracting `FontService` and `GridRenderingPipeline`.

**gpt-5:** agreed, framing it more concretely — `App` still owns too much policy:
- fallback font discovery and cluster-to-font resolution
- grid-to-renderer projection and highlight resolution
- cursor styling
- concrete `SdlWindow` / `NvimRpc` ownership

**Consensus:** this is the first big architectural pressure point. `App` should be reduced to lifecycle and wiring.

**Concrete splits:**
- **`FontService`** — owns `FontManager`, `GlyphCache`, `TextShaper`, fallback chain, `resolve_font_for_text()`, atlas upload decisions. Public API: `get_cluster(text) → AtlasRegion`. No FreeType/HarfBuzz types visible outside `spectre-font`.
- **`GridRenderingPipeline`** — owns highlight resolution, dirty drain, cell→`CellUpdate` conversion, `renderer_->update_cells()`. App calls `pipeline.flush()`.
- **App** becomes: init sequence, event loop, resize coordination.

### 2. `spectre-nvim` must not depend on `spectre-grid`

**claude-sonnet-4-6:** pushed hardest on this. `UiEventHandler` holds a `Grid*` and mutates it directly. The I/O adapter layer owns the model layer. Neither can be tested in isolation.

**gpt-5:** did not conflict with this point — focused elsewhere but consistent with it.

**Consensus:** introducing `IGridSink` is a clean, small, high-value early fix.

```cpp
struct IGridSink {
    virtual void set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool dw) = 0;
    virtual void scroll(int top, int bot, int left, int right, int rows) = 0;
    virtual void clear() = 0;
    virtual void resize(int cols, int rows) = 0;
};
```
`UiEventHandler` takes an `IGridSink*`. `Grid` implements it. `spectre-nvim` CMakeLists drops `spectre-grid`. Both sides become independently testable. `IGridSink` lives in `spectre-types`.

### 3. RPC needs a safer contract

**claude-sonnet-4-6:** emphasised that `MpackValue` is an unsafe tagged union and that RPC protocol handling is under-tested.

**gpt-5:** emphasised that `NvimRpc::request()` returns only `resp.result`, silently discarding Neovim-side errors. `nvim_ui_attach`, `nvim_ui_try_resize`, and every future request have no way to surface failures to the caller.

**Consensus:** the RPC layer needs both a safer value model and a real result contract. Two sub-steps:

1. Introduce `RpcResult`:
```cpp
struct RpcResult {
    MpackValue result;
    MpackValue error;
    bool transport_ok = false;
    bool is_error() const { return !error.is_nil(); }
};
```

2. Replace `MpackValue` with `std::variant<std::nullptr_t, bool, int64_t, uint64_t, double, std::string, MpackArray, MpackMap>`. Add thin typed accessor helpers (`as<T>()`, `is<T>()`) to keep call sites clean. The order of these two sub-steps can be chosen pragmatically — they compose cleanly.

### 4. Tests need to move outward from pure logic toward seams

**claude-sonnet-4-6:** highlighted missing rendering, RPC protocol, process spawn, and input translation tests.

**gpt-5:** agreed and identified the highest-value missing tests:
- RPC failure/error paths
- renderer/backend parity
- app-level startup seams
- double-width scroll/overwrite edge cases

**Consensus:** the current suite is a good foundation (`replay_fixture.h` is genuinely useful infrastructure) but it protects the least risky parts of the code better than the most fragile ones. `spectre-renderer` and `spectre-window` have zero automated coverage.

---

## Where One Agent Added Important Detail

### A. Renderer backend duplication (gpt-5, no conflict from claude)

The Vulkan and Metal backends duplicate CPU-side state management: grid projection, `GpuCell` updates, cursor apply/restore, cursor overlay behaviour. Every cursor or layout bug must be fixed twice.

claude-sonnet-4-6 focused more on `IRenderer` leaking atlas internals; gpt-5 focused on the duplicated implementation. These are compatible observations.

**Consensus:** extract a shared CPU renderer front-end (`RendererBase` or `RendererState`) beneath `IRenderer` and above backend GPU code. Both backends own only GPU resource creation and frame submission.

Composition (a `RendererState` member) is preferred over inheritance — more testable, avoids hot-path virtual dispatch.

### B. Atlas partial-update path is dead code (gpt-5, confirmed by claude)

`GlyphCache` tracks a dirty rectangle. `IRenderer` exposes `update_atlas_region()`. But the app always calls `set_atlas_texture()` for a full upload. The incremental path is never exercised, yet every backend must implement both methods and contributors must reason about a protocol that isn't used.

Combined with claude's finding that `VkRenderer` uploads the entire `GpuCell` buffer (~960 KB for a 200×50 grid) on every dirty cell — cursor blink = 1 MB write — there is a clear case to either properly implement incremental uploads (correct fix) or delete the dead abstraction (simpler fix).

**Consensus:** don't leave it in the middle. Pick one.

### C. `option_set` wired but not consumed (gpt-5, confirmed)

`UiEventHandler` fires `on_option_set` but `App` never wires it. `ambiwidth` and other UI options cannot affect `cluster_cell_width()` or layout logic. The hook exists; the state object and ownership model do not.

**Plan:** add a small `UiOptions` struct. Wire `on_option_set` in App to populate it. Pass it as const ref into width/layout logic. Low effort, real correctness improvement for CJK.

### D. Mouse event structs missing modifier state (gpt-5, confirmed)

`MouseButtonEvent` and `MouseMoveEvent` carry no modifier flags. `NvimInput` always sends an empty modifier string and hardcodes `"left"` for drag. Shift/Ctrl/Alt mouse chords are structurally impossible. Tests cover only the reduced model.

**Plan:** add `uint16_t mod` to mouse event structs (matching `KeyEvent`). Thread through `NvimInput`. Add modifier-aware tests.

### E. Correctness bugs in low-level code (claude-sonnet-4-6)

- **`Grid::scroll()` has no bounds validation** — malformed Neovim events can produce out-of-bounds writes. No single entry-point guard exists.
- **RPC read buffer is fixed at 65536 bytes** — Neovim can produce larger events (many highlights, large buffer contents). Overflow causes silent protocol desync. Replace with `std::vector<char>`.
- **`flush_count_` is dead code** — incremented, never read. Remove it.
- **Atlas size `2048` defined in two places** — `font.h` and `vk_atlas.h`. One constant in `spectre-types`.

---

## What Should Not Be Reopened

Concerns from older reviews that are no longer current:

- Unicode width logic has already been consolidated into `unicode.h`
- native tests now exist for grid, redraw parsing, input translation, and Unicode width
- renderer factory boundary has already been cleaned up
- local workflow and CI are materially better than the earlier review context

This consensus should be treated as the current working document, not the older individual reviews.

---

## Proposed Fix Plan

### Phase 1: Strengthen contracts and cut the worst dependency knot

These must be done before parallel agent work is efficient. Each is self-contained.

| # | Task | Rationale |
|---|------|-----------|
| 1 | `IGridSink` interface in `spectre-types` | Drops `spectre-nvim → spectre-grid` dep. Smallest change, biggest unlock. |
| 2 | `MpackValue → std::variant` | Contained, mechanical. Prerequisite for `RpcResult`. |
| 3 | `RpcResult` type + RPC tests | Exposes Neovim errors. Add attach/resize/failure tests immediately after. |

### Phase 2: Shrink the largest conflict surfaces (parallelisable after Phase 1)

| # | Task |
|---|------|
| 4 | `FontService` extraction — font/glyph/shaping/fallback/atlas decisions leave App |
| 5 | `RendererBase` shared front-end — dedup Vulkan + Metal `gpu_cells_`, cursor, projection |
| 6 | Wire `option_set → UiOptions` — small, independent, correctness fix |

### Phase 3: Close correctness gaps (any order, any agent)

| # | Task |
|---|------|
| 7 | Bounds check `Grid::scroll()` — assert/clamp at entry |
| 8 | Dynamic RPC read buffer — `std::vector<char>` |
| 9 | Mouse modifier state — add `mod` to mouse event structs, add tests |
| 10 | Atlas partial update — implement properly or delete the dead path |
| 11 | Remove `flush_count_` dead code |
| 12 | Unify atlas size constant — one definition in `spectre-types` |

### Phase 4: Broaden verification

| # | Task |
|---|------|
| 13 | Renderer smoke / replay-based parity checks (Vulkan vs Metal) |
| 14 | RPC failure/malformed-frame tests |
| 15 | Double-width scroll/overwrite edge-case tests |
| 16 | App-level startup/shutdown smoke path (CI-friendly) |

### Phase 5: Quality and performance (lower urgency)

| # | Task |
|---|------|
| 17 | `GridRenderingPipeline` extraction from App |
| 18 | Dirty region GPU uploads (skip full buffer write) |
| 19 | Cursor save/restore without GPU readback |
| 20 | Unified logging shim (`log_debug/info/error(subsystem, ...)`) |
| 21 | Bounded notification queue (backpressure on reader thread) |

---

## Open Design Questions

**Q1: `FontService` ownership of atlas upload decisions**
gpt-5 placed atlas decisions in the text service; claude placed them in a `GridRenderingPipeline`. Both agree it should leave App. Recommendation: `FontService` owns the atlas and exposes `needs_flush() / flush_atlas(IRenderer&)`. Pipeline calls it.

**Q2: `RendererBase` — inheritance vs composition**
Composition (`RendererState` member) preferred: more testable, no virtual dispatch overhead on hot paths, cleaner given that both backends already have distinct GPU resource types.

**Q3: `IGridSink` placement**
`spectre-types` — already a shared header-only layer used by both nvim and grid. Putting a pure interface there avoids creating a new dependency edge.

---

## Concrete Suggestions For Multi-Agent Work

1. Prefer "one module owns this policy" over helper logic spread across App, renderers, and tests
2. Extract shared CPU behaviour before adding more backend features
3. Test interfaces and seams, not just local functions
4. Keep this document updated when findings become stale or items are completed
5. Keep refactors small enough that each phase leaves the repo building and testable

---

## Final Take

Both reviews are broadly aligned. The repo does not need a ground-up redesign. It needs a disciplined cleanup of five high-pressure seams:

- App ownership (too much policy)
- RPC contract (silent errors, unsafe value type)
- Neovim/model coupling (`spectre-nvim → spectre-grid`)
- Renderer backend duplication (CPU front-end shared between Vulkan and Metal)
- Seam-level tests (coverage stops just below the most fragile boundaries)

That is a tractable refactor sequence, and it can be done incrementally without destabilising the project.
