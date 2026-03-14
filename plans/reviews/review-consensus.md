# Review Consensus — Spectre
**Sources:** claude-sonnet-4-6, gpt-5
**Format:** Planning conversation — what to fix, why, and in what order

---

## The Big Picture

Both reviews agree on the same diagnosis: **the module boundaries at the directory level are right, but the seams inside those boundaries still collapse too many concerns together.** The library split (`spectre-types`, `spectre-window`, `spectre-renderer`, `spectre-font`, `spectre-grid`, `spectre-nvim`) is sound. The problems are in what leaks across those boundaries and what accumulates in `App`.

The repo is not messy — it is at the stage where a focused refactor of 3-4 seams would make the whole codebase significantly easier to work on in parallel. Neither review found deep algorithmic or correctness problems in the rendering or grid logic. The risk is architectural friction, not broken fundamentals.

---

## Points of Full Agreement (both reviews flagged these)

### A. App is doing too much

Both reviews independently identified `App` as the primary conflict hotspot. It currently owns and directly calls into: window, renderer, font, fallback font chain, glyph cache, text shaper, grid, highlight table, nvim process, RPC, UI event handler, input handler — and performs highlight resolution, glyph lookup, font fallback selection, and atlas upload decisions inline.

This isn't just a style issue. Any feature work that touches fonts, rendering, or input *must* go through `app.cpp`, creating unavoidable merge conflicts for parallel agents.

**Agreed plan:**
- Extract a `FontService` (or `TextService`) that owns `FontManager`, `GlyphCache`, `TextShaper`, fallback font chain, `resolve_font_for_text()`, and atlas upload decisions. Exposes a single `get_cluster(text) → AtlasRegion` call. Nothing FreeType/HarfBuzz-specific visible outside it.
- Extract a `GridRenderingPipeline` that owns highlight resolution, dirty cell drain, cell→`CellUpdate` conversion, and the `renderer_->update_cells()` call. App just calls `pipeline.flush()`.
- App becomes: init sequence, event loop, resize coordination.

### B. `spectre-nvim` depends on `spectre-grid`

`UiEventHandler` holds a `Grid*` and mutates it directly. The I/O layer shouldn't know about the model layer — it makes both untestable in isolation, and forces `spectre-nvim` to carry `spectre-grid` as a dependency.

**Agreed plan:** Introduce `IGridSink`:
```cpp
struct IGridSink {
    virtual void set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool dw) = 0;
    virtual void scroll(int top, int bot, int left, int right, int rows) = 0;
    virtual void clear() = 0;
    virtual void resize(int cols, int rows) = 0;
};
```
`UiEventHandler` takes an `IGridSink*`. `Grid` implements it. `spectre-nvim` drops its `spectre-grid` link dependency.

### C. `MpackValue` is unsafe

Both reviews flagged the hand-rolled tagged union. Every value allocates space for every type. `.as_int()` on a string is UB with no diagnostic. New message types require modifying 100+ line switch statements in multiple files.

**Agreed plan:** Replace with `std::variant`. Illegal access throws `std::bad_variant_access`. Switch statements become `std::visit`. This is a contained, mechanical change with a clear correctness improvement.

### D. Testing gaps in the same areas

Both reviews independently found the same holes:
- No RPC failure/error path tests
- No renderer-level tests (either backend)
- No input translation tests
- `replay_fixture.h` is good infrastructure that isn't being used enough

---

## Points Raised by One Review, Endorsed Here

### E. Vulkan and Metal CPU front-end are duplicated (gpt-5)

`set_grid_size()`, `update_cells()`, `set_cursor()`, `apply_cursor()`, and `restore_cursor()` exist in nearly identical form in both `vk_renderer.cpp` and `metal_renderer.mm`. Every cursor or layout bug has to be fixed twice. The recent cursor regression is a concrete example of this risk.

**Plan:** Extract a `RendererBase` or `CpuRendererFrontend` class that owns `gpu_cells_[]`, cursor overlay state, cell-size/grid-size projection, and cursor save/restore. Both backends inherit from it or delegate to it. Backend-specific code handles only GPU resource creation and frame submission.

This is distinct from the `IRenderer` interface — it's a shared *implementation* layer, not a new abstraction.

### F. RPC error responses are silently dropped (gpt-5)

`NvimRpc::request()` returns only `resp.result`, discarding `resp.error`. So `nvim_ui_attach`, `nvim_ui_try_resize`, and every other request cannot surface Neovim-side errors. The app has no way to know whether attach succeeded or why it failed.

**Plan:** Return a small result type:
```cpp
struct RpcResult {
    MpackValue result;
    MpackValue error;
    bool transport_ok = false;
    bool is_error() const { return !error.is_nil(); }
};
```
Then add tests for attach, resize, and failure paths. This would also make `MpackValue → std::variant` migration cleaner, since `RpcResult` becomes a well-typed container.

### G. Atlas partial-update path is dead code (gpt-5)

`GlyphCache` tracks a dirty rectangle. `IRenderer` exposes `update_atlas_region()`. But the app always calls `set_atlas_texture()` for a full upload. This leaves a dead abstraction in the renderer interface that every backend must implement but nothing calls.

**Plan:** Either actually use `update_atlas_region()` (correct fix, also addresses the full-buffer-upload perf issue from the other review), or remove both the dirty rect tracking and `update_atlas_region()` from the interface until it's needed. Dead abstractions that span two backends are expensive to maintain.

### H. Mouse events missing modifier state (gpt-5)

`MouseButtonEvent` and `MouseMoveEvent` don't carry modifier flags. `NvimInput` always sends an empty modifier string and hardcodes `"left"` for drag. This blocks Shift/Ctrl/Alt mouse chords from ever working and the test model covers only the reduced version, making it easy to overlook.

**Plan:** Add `uint16_t mod` to the mouse event structs (parallel with `KeyEvent`). Thread modifier state through `NvimInput::on_mouse_button()` and `on_mouse_move()`. Add tests.

### I. `option_set` is wired but not consumed (gpt-5)

`UiEventHandler` exposes `on_option_set` but `App` never wires it. The `ambiwidth` option (and future options) therefore can't affect the `cluster_cell_width()` logic that drives column counting. The hook already exists — it just isn't connected to any state.

**Plan:** Add a small `UiOptions` struct. Wire `on_option_set` in App to update it. Pass it (or a const ref) into `cluster_cell_width()`. This is low-effort and closes a real correctness gap for CJK users.

### J. Font types leak through public API (claude-sonnet-4-6)

`FontManager::face()` returns `FT_Face`. `FontManager::hb_font()` returns `hb_font_t*`. App calls FreeType directly. Covered under item A (FontService extraction) — once `FontService` wraps this, the public API becomes opaque.

### K. Bounds checking gaps (claude-sonnet-4-6)

`Grid::scroll()` has no entry-point validation — malformed Neovim events can produce out-of-bounds writes. `rpc.cpp` has a fixed 65536-byte read buffer that Neovim can exceed.

**Plan:** Add asserts (or hard bounds clamps) at the top of `Grid::scroll()`. Make the RPC read buffer dynamic (`std::vector<char>`).

---

## Suggested Fix Order

This ordering minimises conflicts between workstreams and delivers the most parallelism unlock early.

### Phase 1 — Break the dependency knots (do in order, each unblocks the next)

1. **`IGridSink` interface** — drop `spectre-nvim → spectre-grid` link. Smallest change, biggest dependency unlock. Write tests for `UiEventHandler` in isolation once done.

2. **`MpackValue → std::variant`** — contained, mechanical. Do before RPC result type since it simplifies that work.

3. **`RpcResult` type** — expose errors from `request()`. Add tests for attach/resize/failure paths.

### Phase 2 — Extract services (can be parallelised after Phase 1)

4. **`FontService`** — extract from App. FontManager, GlyphCache, TextShaper, fallback chain, `resolve_font_for_text()`. App calls `font_service_.get_cluster(text)`. No FreeType/HarfBuzz types in app.h.

5. **`RendererBase` shared front-end** — extract duplicated `gpu_cells_`, cursor overlay, projection logic from Vulkan and Metal. Both backends delegate to it.

6. **Wire `option_set` → `UiOptions`** — small, independent, closes a real correctness gap.

### Phase 3 — Correctness and cleanup (can be done by anyone, any order)

7. Bounds check `Grid::scroll()` — add asserts/clamps at entry
8. Dynamic RPC read buffer — `std::vector<char>` replacing fixed array
9. Mouse modifier state — add `mod` to mouse event structs
10. Atlas partial update — either implement `update_atlas_region()` properly or delete the dead path
11. Remove `flush_count_` dead code
12. Unify atlas size constant (one definition in `spectre-types`)
13. Add modifier-aware mouse input tests
14. Add input key translation tests (table-driven)

### Phase 4 — Quality (lower urgency)

15. Unified logging shim
16. Bounded notification queue
17. `GridRenderingPipeline` extraction from App
18. Dirty region GPU uploads (skip full buffer write)
19. Cursor save/restore without GPU readback

---

## What Not to Change

Both reviews agree the following are well-designed and should not be refactored:

- Module directory structure and CMakeLists layout
- `spectre-types` as header-only shared layer
- Two-pass instanced rendering (BG then FG quads)
- Shelf-packing atlas with incremental upload (fix the dead code, don't remove the concept)
- Reader thread / main thread separation with mutex-protected queue
- `unicode.h` cluster width handling
- The existing `replay_fixture.h`-based test infrastructure

---

## Open Questions

1. **`GridRenderingPipeline` vs keeping it in App** — gpt-5 described it as a `FontService` concern (font service decides when to upload atlas); claude-sonnet-4-6 described it as a separate pipeline object. Both agree the logic should leave App; the question is where it lives. Recommend: start with FontService owning atlas decisions, revisit pipeline extraction as a Phase 4 item.

2. **`RendererBase` as inheritance vs composition** — the shared renderer front-end could be a base class both backends inherit, or a `RendererState` object both delegate to. Composition is more testable; inheritance is less code. Decision deferred to whoever picks up item 5.

3. **`std::variant` for MpackValue — `std::visit` verbosity** — the variant approach is correct but `std::visit` can be noisy at call sites. Consider a thin set of typed accessor helpers (`as<int64_t>(val)`, `is<std::string>(val)`) on top of the variant to keep call sites clean while keeping type safety.
