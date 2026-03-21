#include "support/test_support.h"

#include <draxul/grid_rendering_pipeline.h>
#include <draxul/ui_request_worker_state.h>

#include <draxul/grid.h>
#include <draxul/highlight.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// Minimal fakes reused from grid_rendering_pipeline_tests — kept local so this
// file compiles standalone and does not introduce a shared dependency.
// ---------------------------------------------------------------------------

namespace
{

class FakeGlyphAtlas final : public IGlyphAtlas
{
public:
    AtlasRegion resolve_cluster(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        ++resolve_calls;
        atlas_dirty_ = true;
        auto it = glyphs_.find(text);
        if (it != glyphs_.end())
            return it->second;
        return {};
    }

    int ligature_cell_span(const std::string&, bool /*is_bold*/, bool /*is_italic*/) override
    {
        return 0;
    }

    bool atlas_dirty() const override
    {
        return atlas_dirty_;
    }

    bool consume_atlas_reset() override
    {
        return false;
    }

    void clear_atlas_dirty() override
    {
        atlas_dirty_ = false;
    }

    const uint8_t* atlas_data() const override
    {
        return atlas_.data();
    }

    int atlas_width() const override
    {
        return 2;
    }

    int atlas_height() const override
    {
        return 2;
    }

    AtlasDirtyRect atlas_dirty_rect() const override
    {
        return { 0, 0, 2, 2 };
    }

    void register_glyph(const std::string& text, AtlasRegion region)
    {
        glyphs_[text] = region;
    }

    int resolve_calls = 0;

private:
    bool atlas_dirty_ = false;
    std::vector<uint8_t> atlas_ = std::vector<uint8_t>(16, 0x00);
    std::unordered_map<std::string, AtlasRegion> glyphs_;
};

class FakeRenderer final : public IRenderer
{
public:
    bool initialize(IWindow&) override
    {
        return true;
    }

    void shutdown() override {}

    bool begin_frame() override
    {
        return true;
    }

    void end_frame() override {}

    void set_grid_size(int, int) override {}

    void update_cells(std::span<const CellUpdate>) override {}

    void set_overlay_cells(std::span<const CellUpdate>) override {}

    void set_atlas_texture(const uint8_t*, int, int) override
    {
        ++full_atlas_uploads;
    }

    void update_atlas_region(int, int, int, int, const uint8_t*) override
    {
        ++region_uploads;
    }

    void set_cursor(int, int, const CursorStyle&) override {}

    void resize(int, int) override {}

    std::pair<int, int> cell_size_pixels() const override
    {
        return { 10, 20 };
    }

    void set_cell_size(int, int) override {}

    void set_ascender(int) override {}

    bool initialize_imgui_backend() override
    {
        return true;
    }

    void shutdown_imgui_backend() override {}

    void rebuild_imgui_font_texture() override {}

    void begin_imgui_frame() override {}

    void set_imgui_draw_data(const ImDrawData*) override {}

    void request_frame_capture() override {}

    std::optional<CapturedFrame> take_captured_frame() override
    {
        return std::nullopt;
    }

    int padding() const override
    {
        return 0;
    }

    void set_default_background(Color) override {}
    void set_scroll_offset(float) override {}
    void register_render_pass(std::shared_ptr<IRenderPass>) override {}
    void unregister_render_pass() override {}

    int full_atlas_uploads = 0;
    int region_uploads = 0;
};

// Build a grid with N cells all dirty
Grid make_populated_grid(int cols, int rows)
{
    Grid grid;
    grid.resize(cols, rows);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            grid.set_cell(c, r, "A", 0);
    return grid;
}

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void run_font_size_tests()
{
    // ------------------------------------------------------------------
    // Step 1 — grid.mark_all_dirty() marks every cell
    // ------------------------------------------------------------------
    run_test("font size change: mark_all_dirty marks every cell in the grid", []() {
        Grid grid;
        grid.resize(10, 5);
        grid.clear_dirty();

        expect_eq(grid.dirty_cell_count(), size_t(0),
            "newly cleared grid should have no dirty cells");

        grid.mark_all_dirty();

        expect_eq(grid.dirty_cell_count(), size_t(50),
            "mark_all_dirty should dirty all 10x5 cells");
    });

    run_test("font size change: mark_all_dirty on a single-row grid marks every column", []() {
        Grid grid;
        grid.resize(80, 1);
        grid.clear_dirty();
        grid.mark_all_dirty();

        expect_eq(grid.dirty_cell_count(), size_t(80),
            "mark_all_dirty should mark all 80 columns in a single-row grid");
    });

    // ------------------------------------------------------------------
    // Step 2 — force_full_atlas_upload causes a full atlas upload on next flush
    // ------------------------------------------------------------------
    run_test("font size change: force_full_atlas_upload triggers a full atlas texture upload on next flush", []() {
        // Set up a 2x1 grid with two known cells so the pipeline has work to do.
        Grid grid;
        grid.resize(2, 1);
        grid.set_cell(0, 0, "A", 0);
        grid.set_cell(1, 0, "B", 0);

        HighlightTable highlights;
        FakeGlyphAtlas atlas;
        atlas.register_glyph("A", { 0.0f, 0.0f, 0.25f, 0.5f, 1, 2, 7, 9, false });
        atlas.register_glyph("B", { 0.25f, 0.0f, 0.5f, 0.5f, 2, 3, 8, 10, false });

        FakeRenderer renderer;
        GridRenderingPipeline pipeline(grid, highlights, atlas);
        pipeline.set_renderer(&renderer);

        // Perform a first flush to settle the baseline state.
        pipeline.flush();
        const int uploads_after_first_flush = renderer.full_atlas_uploads;

        // Simulate what change_font_size does: mark all cells dirty and force
        // a full atlas upload.
        grid.mark_all_dirty();
        pipeline.force_full_atlas_upload();
        pipeline.flush();

        expect(renderer.full_atlas_uploads > uploads_after_first_flush,
            "force_full_atlas_upload should produce at least one additional full atlas texture upload");
    });

    run_test("font size change: force_full_atlas_upload with no dirty cells still schedules a full upload", []() {
        Grid grid;
        grid.resize(2, 1);
        grid.set_cell(0, 0, "A", 0);
        grid.set_cell(1, 0, "B", 0);

        HighlightTable highlights;
        FakeGlyphAtlas atlas;
        atlas.register_glyph("A", { 0.0f, 0.0f, 0.25f, 0.5f, 1, 2, 7, 9, false });
        atlas.register_glyph("B", { 0.25f, 0.0f, 0.5f, 0.5f, 2, 3, 8, 10, false });

        FakeRenderer renderer;
        GridRenderingPipeline pipeline(grid, highlights, atlas);
        pipeline.set_renderer(&renderer);

        // First flush clears dirty state.
        pipeline.flush();
        const int uploads_after_first_flush = renderer.full_atlas_uploads;

        // force_full_atlas_upload with no pending dirty cells — next flush
        // should still issue the full upload.
        pipeline.force_full_atlas_upload();
        pipeline.flush();

        expect(renderer.full_atlas_uploads > uploads_after_first_flush,
            "force_full_atlas_upload should schedule the upload even when no cells are dirty");
    });

    // ------------------------------------------------------------------
    // Step 3 — UiRequestWorkerState queues a resize with correct dimensions
    // ------------------------------------------------------------------
    run_test("font size change: UiRequestWorkerState queues resize with new dimensions", []() {
        UiRequestWorkerState state;
        state.start();

        // Simulate change_font_size issuing a resize after recalculating layout
        const int new_cols = 100;
        const int new_rows = 30;
        const bool accepted = state.request_resize(new_cols, new_rows, "font resize");

        expect(accepted, "running worker state should accept a resize request");
        expect(state.has_pending_request(), "resize request should be pending");

        auto pending = state.take_pending_request();
        expect(pending.has_value(), "pending request should be retrievable");
        expect_eq(pending->cols, new_cols,
            "queued resize should carry the new column count");
        expect_eq(pending->rows, new_rows,
            "queued resize should carry the new row count");
        expect_eq(pending->reason, std::string("font resize"),
            "queued resize should preserve the originating reason");
    });

    run_test("font size change: UiRequestWorkerState coalesces rapid font size changes to the latest resize", []() {
        UiRequestWorkerState state;
        state.start();

        // Simulate user holding down font-increase shortcut — several rapid
        // font size changes each compute different new dimensions.
        state.request_resize(80, 24, "font resize");
        state.request_resize(78, 23, "font resize");
        state.request_resize(76, 22, "font resize");

        auto pending = state.take_pending_request();
        expect(pending.has_value(), "there should be exactly one pending request after rapid changes");
        expect_eq(pending->cols, 76,
            "coalescing should keep the last submitted column count");
        expect_eq(pending->rows, 22,
            "coalescing should keep the last submitted row count");
    });

    run_test("font size change: UiRequestWorkerState does not queue a resize when stopped", []() {
        UiRequestWorkerState state;
        state.start();
        state.stop();

        const bool accepted = state.request_resize(80, 24, "font resize");
        expect(!accepted, "stopped worker state should reject resize requests");
        expect(!state.has_pending_request(),
            "no request should be pending after rejection");
    });

    // ------------------------------------------------------------------
    // Combined cascade: mark_all_dirty + force_full_atlas_upload together
    // ------------------------------------------------------------------
    run_test("font size change cascade: dirty grid plus forced atlas upload produces full re-render on flush", []() {
        // Replicate the full change_font_size cascade steps that don't
        // require a live TextService or IWindow:
        //   1. grid_.mark_all_dirty()
        //   2. grid_pipeline_.force_full_atlas_upload()
        //   3. grid_pipeline_.flush()   (→ full atlas upload + cell updates)
        //   4. UiRequestWorkerState::request_resize(new_cols, new_rows)

        const int cols = 8;
        const int rows = 3;

        Grid grid = make_populated_grid(cols, rows);
        grid.clear_dirty();

        HighlightTable highlights;
        FakeGlyphAtlas atlas;
        atlas.register_glyph("A", { 0.0f, 0.0f, 0.25f, 0.5f, 1, 2, 7, 9, false });

        FakeRenderer renderer;
        GridRenderingPipeline pipeline(grid, highlights, atlas);
        pipeline.set_renderer(&renderer);

        // Baseline flush — clears dirty state, establishes initial uploads.
        pipeline.flush();
        const int baseline_uploads = renderer.full_atlas_uploads;
        const int baseline_resolves = atlas.resolve_calls;

        // --- Font size change cascade ---

        // Step 1: mark all cells dirty (mirrors grid_.mark_all_dirty())
        grid.mark_all_dirty();
        expect_eq(grid.dirty_cell_count(), size_t(cols * rows),
            "all cells should be dirty after mark_all_dirty");

        // Step 2: flag forced full atlas upload (mirrors grid_pipeline_.force_full_atlas_upload())
        pipeline.force_full_atlas_upload();

        // Step 3: flush (mirrors grid_pipeline_.flush() in change_font_size)
        pipeline.flush();

        expect(renderer.full_atlas_uploads > baseline_uploads,
            "cascade flush should issue a full atlas texture upload");
        expect(atlas.resolve_calls > baseline_resolves,
            "cascade flush should re-resolve glyphs for all dirty cells");
        expect_eq(grid.dirty_cell_count(), size_t(0),
            "grid should have no dirty cells after a successful cascade flush");

        // Step 4: resize RPC request (mirrors queue_resize_request in change_font_size)
        const int new_cols = cols - 1; // smaller font → more columns, but we use any change
        const int new_rows = rows - 1;
        UiRequestWorkerState worker_state;
        worker_state.start();
        worker_state.request_resize(new_cols, new_rows, "font resize");

        auto pending = worker_state.take_pending_request();
        expect(pending.has_value(), "resize RPC should be queued after cascade");
        expect_eq(pending->cols, new_cols, "resize request carries correct column count");
        expect_eq(pending->rows, new_rows, "resize request carries correct row count");
    });
}
