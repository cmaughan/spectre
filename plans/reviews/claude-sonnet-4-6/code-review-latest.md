# Code Review — Spectre
**Reviewer:** claude-sonnet-4-6
**Scope:** Full codebase — all libs, app, tests, build system

---

## Overall Assessment

The architecture is well-shaped. Module boundaries (`spectre-types`, `spectre-window`, `spectre-renderer`, `spectre-font`, `spectre-grid`, `spectre-nvim`) follow a sensible dependency hierarchy and the public interfaces are appropriately thin. The Vulkan rendering design (procedural quads, host-visible SSBO, shelf-packed atlas) is correct and efficient for a terminal emulator.

The main problems are concentrated in two places: **App** doing too much, and **leaking implementation types** (FreeType, HarfBuzz, MpackValue) across module boundaries. Fixing those two things would unlock independent parallel work on each module.

---

## Critical — Blocks parallel development

### 1. Monolithic `App` class (`app/app.cpp`, 448 lines)

App directly owns and orchestrates: window, renderer, font, fallback font chain, glyph cache, text shaper, grid, highlight table, nvim process, RPC, UI event handler, input handler. It performs highlight resolution, glyph lookup, atlas upload logic, and font fallback resolution inline in `update_grid_to_renderer()`.

Any change to font metrics, the renderer interface, glyph cache strategy, or highlight table format forces edits to `app.cpp`. There is no boundary a second developer can work behind without touching this file.

**Concrete splits that would help:**
- `FontService` — owns `FontManager`, `GlyphCache`, `TextShaper`, fallback chain, `resolve_font_for_text()`. Exposes a single `get_cluster(text) → AtlasRegion` call.
- `GridRenderingPipeline` — owns highlight resolution, cell→`CellUpdate` conversion, dirty drain, atlas upload triggering. Sits between `Grid`/`HighlightTable` and `IRenderer`.
- App becomes: init sequence + event loop + resize handler.

### 2. `spectre-nvim` depends on `spectre-grid` (`nvim/CMakeLists.txt`)

`UiEventHandler` holds a `Grid*` and mutates it directly (`grid_->set_cell(...)`, `grid_->scroll(...)`, `grid_->clear()`). This pulls the entire grid domain model into the I/O adapter layer, making it impossible to test either in isolation.

**Fix:** Replace the `Grid*` with callbacks or a narrow `IGridSink` interface:
```cpp
struct IGridSink {
    virtual void set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool dw) = 0;
    virtual void scroll(int top, int bot, int left, int right, int rows) = 0;
    virtual void clear() = 0;
    virtual void resize(int cols, int rows) = 0;
};
```
`UiEventHandler` knows nothing about `Grid`; grid tests need no RPC stack.

### 3. `MpackValue` is an unsafe manual tagged union (`nvim.h`)

Every value stores fields for every possible type simultaneously. Access is unchecked — calling `.as_int()` on a string value is undefined behaviour with no diagnostic. Every new message type requires additions to `read_value()` and `write_value()` switch statements that are 100+ lines long.

**Fix:** `std::variant<std::nullptr_t, bool, int64_t, uint64_t, double, std::string, MpackArray, MpackMap>` with `std::get` / `std::visit`. Illegal access becomes a thrown `std::bad_variant_access` rather than silent corruption.

### 4. Font implementation types exposed in public API (`font.h`)

`FontManager::face()` returns `FT_Face`. `FontManager::hb_font()` returns `hb_font_t*`. App calls `FT_Load_Glyph` directly. Fallback font selection (`resolve_font_for_text`) runs in App and touches FreeType face objects.

Any font backend change (DirectWrite, CoreText, alternate shaping) requires rewriting `app.cpp` alongside `spectre-font`. The font module cannot be mocked or substituted in tests.

**Fix:** Provide a higher-level method on `FontManager` or `GlyphCache`:
```cpp
ClusterInfo get_cluster(const std::string& utf8_text);
bool can_render(const std::string& utf8_text) const;
```
No FreeType or HarfBuzz types appear outside `spectre-font`.

---

## Major — Architectural debt

### 5. `IRenderer` leaks glyph cache internals

`set_atlas_texture(const uint8_t*, int w, int h)` and `update_atlas_region(...)` expose raw pixel pointers and atlas dimensions. These are implementation details of `GlyphCache`. The atlas size (2048) is duplicated in `font.h` and `vk_atlas.h` — if one changes without the other the atlas format breaks silently.

The renderer should not know about atlas packing. Options: push uploads into a renderer-owned `IGlyphAtlas`, or treat atlas as an opaque resource handle and let the font module populate it.

### 6. Grid size is stored in three places

`Grid::cols_/rows_`, `App::grid_cols_/rows_`, `VkRenderer::grid_cols_/rows_` are all independently maintained. A resize touches all three via different code paths. There is no single source of truth; they can diverge if any path is missed.

### 7. Dirty tracking reinvented four times

The pattern "track what needs uploading" appears as:
- `Grid` per-cell dirty flag
- `GlyphCache::dirty_` boolean + `dirty_rect_`
- `VkRenderer::needs_descriptor_update_`
- Implicit in `atlas_needs_full_upload_` in App

Each is a bespoke solution. A shared `DirtyRegion` or `DirtyFlag<T>` would make the pattern explicit and consistent.

### 8. Callback ownership is fragile

`IWindow` has 6 `std::function` callbacks; `UiEventHandler` has 3. All are assigned in `App::initialize()` as lambdas capturing `this`. There is no disconnection mechanism — if the window outlives App (possible in abnormal shutdown), the callbacks fire into a dangling pointer. No tests can exercise a callback without constructing the entire App.

### 9. Vulkan: full buffer upload on every dirty cell

`update_grid_to_renderer()` calls `renderer_->update_cells(updates)` which currently copies the changed `CellUpdate` structs but `VkRenderer` uploads the entire `GpuCell` buffer to the GPU regardless of how many cells changed. For a 200×50 grid (10,000 cells × 96 bytes = ~960 KB), a single cursor blink causes a ~1 MB write. The grid has dirty tracking; it should be used here.

### 10. Cursor rendering implies GPU readback

`apply_cursor()` and `restore_cursor()` read back the current cell from the GPU buffer to save/restore state around cursor rendering. This forces a CPU stall on the GPU timeline every frame the cursor is visible.

---

## Significant — Code quality and correctness

### 11. Duplicate UTF-8 decoding with divergent implementations

`grid.cpp` has `decode_first_codepoint()` and `ui_events.cpp` has `utf8_decode_next()`. They implement the same algorithm but the `grid.cpp` version has a bounds checking gap in the 2-byte case. A single authoritative implementation should live in `spectre-types/include/spectre/unicode.h` (which already has other Unicode utilities).

### 12. No bounds checking in `Grid::scroll()`

The scroll implementation (`grid.cpp`) assumes `top < bot ≤ rows_` and `left < right ≤ cols_`. If a malformed Neovim event violates these preconditions:
```cpp
for (int r = top; r < bot - rows; r++) {
    cells_[r * cols_ + c] = cells_[(r + rows) * cols_ + c];
```
The loop can iterate backwards, or the second index can walk past the end of the allocation. Should have asserts or explicit range validation at the entry point.

### 13. RPC read buffer is fixed at 65536 bytes

`char buf[65536]` in `rpc.cpp`. Neovim can produce events larger than this (large buffer contents, long file paths, many highlights). Buffer overflow means silently truncated messages or protocol desync. Should be dynamic or at minimum checked against the actual read size.

### 14. `flush_count_` is dead code

Incremented in `on_flush()`, never read anywhere. Either instrument it for debugging or remove it.

### 15. Inconsistent error handling across modules

- `FontManager::initialize` → returns `bool`
- `Grid::set_cell` out-of-bounds → silent return, no feedback
- `VkPipeline` shader file missing → `fprintf` + continue (may render with null pipeline)
- `GlyphCache` atlas full → `fprintf` + returns `false`, caller ignores
- RPC `read_failed_` → atomic flag, never reset, permanent failure after transient I/O error

No module uses a consistent strategy. Makes it impossible to write reliable recovery logic.

### 16. Fallback font vector can hold partially-initialized entries

In `initialize_fallback_fonts()`, a `FallbackFont` is `push_back`'d then conditionally `pop_back`'d if initialization fails. If the move or copy in `push_back` partially succeeds before the shaper init fails, the destructor path is non-trivial. Use `emplace_back` with a validated factory or construct outside the vector before committing.

### 17. Notification queue has no backpressure

The reader thread pushes `RpcNotification` objects onto an `std::queue` protected by a mutex with no size limit. If the main thread stalls (e.g., resizing, heavy atlas upload), Neovim continues sending events and memory grows unbounded. A bounded ring buffer or drop-oldest policy would bound memory use.

### 18. Logging is four different mechanisms

`fprintf(stderr, "[spectre]...")`, `fprintf(stderr, "[rpc]...")`, plain `fprintf(stderr, ...)`, and `SDL_Log(...)` are all in use. There is no log level, no way to suppress or redirect output, no timestamps. A minimal `log_debug/log_info/log_error(subsystem, fmt, ...)` shim would unify these with zero overhead in release builds.

---

## Testing gaps

### 19. No rendering tests

The Vulkan pipeline has zero test coverage. Shader correctness, cell positioning math, cursor rendering, atlas upload — all tested only by running the application. At minimum the cell-to-pixel coordinate math and glyph offset calculation (`cell_h - ascender + bearing_y`) should have unit tests since these are subtle and break invisibly.

### 20. No RPC protocol tests

`MpackValue` serialisation/deserialisation, message framing, and notification dispatch have no tests. The `replay_fixture.h` support infrastructure exists and works — it should cover more of the RPC decode/encode paths.

### 21. No input translation tests

`input.cpp` key translation is a large switch with modifier logic. A key that maps incorrectly causes silent misbehaviour inside Neovim. The translation table should be table-driven and trivially testable.

### 22. No process spawn / pipe test

`NvimProcess::spawn()` is untested. A test that launches a minimal process, sends msgpack down the pipe, and reads back a response would give confidence that the IPC layer works across build configurations.

### 23. Test framework has no verbose mode

The custom `expect()`/`expect_eq()` macros only print on failure and don't identify which test was running at failure without reconstructing from context. Standard test framework (Catch2, doctest — header-only, easily FetchContent'd) would give better diagnostics for free.

---

## Minor / quick wins

| Item | Location | Fix |
|------|----------|-----|
| Atlas size `2048` defined twice | `font.h:63`, `vk_atlas.h:14` | Single constant in `spectre-types` |
| `flush_count_` written, never read | `app.h:65`, `app.cpp:224` | Remove |
| `TextInputEvent.text` is `const char*` | `events.h` | Document lifetime or use `std::string_view` |
| `vk_pipeline.cpp` opens shader file with no size check | pipeline init | Validate file size > 0 before creating `VkShaderModule` |
| `NvimProcess` shutdown timeout hardcoded to 2s | `nvim_process.cpp:102` | Make configurable or at least a named constant |
| RPC request timeout hardcoded to 5s | `rpc.cpp:11` | Named constant |
| Physical device prefers discrete, no fallback | `vk_context.cpp:43` | Log a warning if integrated is selected |
| Vulkan API version hardcoded to 1.2 | `vk_context.cpp:21` | Comment why; consider trying 1.1 as fallback |
| `change_font_size` sends resize before awaiting confirmation | `app.cpp:395` | Document the race or gate the resize |

---

## Module dependency violations

```
Current (problematic):
  spectre-nvim → spectre-grid   ← I/O layer owns model layer

Should be:
  spectre-nvim → (IGridSink callback) ← injected at app layer
  app → spectre-grid
  app → spectre-nvim
```

All other dependencies flow correctly downward.

---

## Recommended priority order

**Do first (unlock parallel work):**
1. `IGridSink` interface — decouple `UiEventHandler` from `Grid`
2. `FontService` extraction — move fallback logic + glyph resolution out of App
3. Replace `MpackValue` enum with `std::variant`

**Do next (correctness):**
4. Bounds check `Grid::scroll()`
5. Dynamic RPC read buffer
6. Single atlas size constant
7. Remove `flush_count_`

**Do later (quality):**
8. Dirty region GPU uploads (skip full buffer write when few cells change)
9. Unified logging shim
10. Table-driven input translation with tests
11. Bounded notification queue

---

## What is already good

- Module header structure and CMakeLists layout — clean, easy to navigate
- `spectre-types` as a pure header-only shared type layer — correct design
- Two-pass rendering (BG then FG) with instanced draws — efficient
- Shelf-packing glyph atlas with incremental upload — correct approach
- `unicode.h` — comprehensive and correct cluster width handling
- Reader thread / main thread separation with mutex-protected queue — correct model
- CLAUDE.md and AGENTS.md — well-maintained onboarding docs
- Existing tests in `tests/` — good coverage of grid and UI event logic
