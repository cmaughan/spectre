#include "support/test_support.h"

#include <draxul/grid_rendering_pipeline.h>

#include <draxul/grid.h>
#include <draxul/highlight.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

class FakeGlyphAtlas final : public IGlyphAtlas
{
public:
    explicit FakeGlyphAtlas(int resets_remaining = 0)
        : resets_remaining_(resets_remaining)
    {
        atlas_.assign(16, 0x7F);
        glyphs_["A"] = { 0.0f, 0.0f, 0.25f, 0.5f, 1, 2, 7, 9, false };
        glyphs_["B"] = { 0.25f, 0.0f, 0.5f, 0.5f, 2, 3, 8, 10, false };
        glyphs_["-"] = { 0.5f, 0.0f, 0.625f, 0.5f, 1, 1, 6, 3, false };
        glyphs_[">"] = { 0.625f, 0.0f, 0.75f, 0.5f, 1, 1, 6, 8, false };
        glyphs_["X"] = { 0.75f, 0.0f, 0.875f, 0.5f, 1, 2, 7, 9, false };
        glyphs_["->"] = { 0.0f, 0.5f, 0.375f, 1.0f, 1, 2, 18, 9, false };
        ligature_spans_["->"] = 2;
    }

    AtlasRegion resolve_cluster(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        ++resolve_calls;
        resolved_texts.push_back(text);
        atlas_dirty_ = true;

        auto it = glyphs_.find(text);
        if (it != glyphs_.end())
            return it->second;
        return {};
    }

    int ligature_cell_span(const std::string& text, bool /*is_bold*/, bool /*is_italic*/) override
    {
        auto it = ligature_spans_.find(text);
        return it != ligature_spans_.end() ? it->second : 0;
    }

    bool atlas_dirty() const override
    {
        return atlas_dirty_;
    }

    bool consume_atlas_reset() override
    {
        if (resets_remaining_ <= 0)
            return false;

        --resets_remaining_;
        atlas_dirty_ = false;
        return true;
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

    int resolve_calls = 0;
    std::vector<std::string> resolved_texts;

private:
    int resets_remaining_ = 0;
    bool atlas_dirty_ = false;
    std::vector<uint8_t> atlas_;
    std::unordered_map<std::string, AtlasRegion> glyphs_;
    std::unordered_map<std::string, int> ligature_spans_;
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

    void update_cells(std::span<const CellUpdate> updates) override
    {
        update_batches.emplace_back(updates.begin(), updates.end());
    }

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
        return 1;
    }

    void set_default_background(Color) override {}
    void set_scroll_offset(float) override {}
    void register_render_pass(std::shared_ptr<IRenderPass>) override {}
    void unregister_render_pass() override {}

    int full_atlas_uploads = 0;
    int region_uploads = 0;
    std::vector<std::vector<CellUpdate>> update_batches;
};

Grid make_grid()
{
    Grid grid;
    grid.resize(2, 1);
    grid.clear_dirty();
    grid.set_cell(0, 0, "A", 0, false);
    grid.set_cell(1, 0, "B", 0, false);
    return grid;
}

Grid make_ligature_grid()
{
    Grid grid;
    grid.resize(2, 1);
    grid.clear_dirty();
    grid.set_cell(0, 0, "-", 0, false);
    grid.set_cell(1, 0, ">", 0, false);
    return grid;
}

} // namespace

void run_grid_rendering_pipeline_tests()
{
    run_test("grid rendering pipeline retries once after an atlas reset", []() {
        Grid grid = make_grid();
        HighlightTable highlights;
        FakeGlyphAtlas atlas(1);
        FakeRenderer renderer;
        GridRenderingPipeline pipeline(grid, highlights, atlas);
        pipeline.set_renderer(&renderer);

        pipeline.flush();

        expect_eq(atlas.resolve_calls, 4, "dirty cells should be replayed after the first atlas reset");
        expect_eq(static_cast<int>(atlas.resolved_texts.size()), 4, "both glyphs should be resolved twice");
        expect_eq(atlas.resolved_texts[0], std::string("A"), "first pass keeps the first glyph");
        expect_eq(atlas.resolved_texts[1], std::string("B"), "first pass keeps the second glyph");
        expect_eq(atlas.resolved_texts[2], std::string("A"), "retry replays the first glyph");
        expect_eq(atlas.resolved_texts[3], std::string("B"), "retry replays the second glyph");
        expect_eq(renderer.full_atlas_uploads, 1, "retry should force one full atlas upload");
        expect_eq(renderer.region_uploads, 0, "retry should not fall back to a region upload");
        expect_eq(static_cast<int>(renderer.update_batches.size()), 1, "successful retry should emit one cell update batch");
        expect_eq(static_cast<int>(renderer.update_batches[0].size()), 2, "both dirty cells should be sent to the renderer");
        expect_eq(renderer.update_batches[0][0].glyph.width, 7, "first glyph survives the retry");
        expect_eq(renderer.update_batches[0][1].glyph.width, 8, "second glyph survives the retry");
        expect_eq(grid.dirty_cell_count(), size_t(0), "successful retry clears the grid dirty set");
    });

    run_test("grid rendering pipeline gives up after a second atlas reset", []() {
        Grid grid = make_grid();
        HighlightTable highlights;
        FakeGlyphAtlas atlas(2);
        FakeRenderer renderer;
        GridRenderingPipeline pipeline(grid, highlights, atlas);
        pipeline.set_renderer(&renderer);

        pipeline.flush();

        expect_eq(atlas.resolve_calls, 4, "the retry loop should stop after two attempts");
        expect_eq(static_cast<int>(renderer.update_batches.size()), 0, "double reset should not emit partial cell updates");
        expect_eq(renderer.full_atlas_uploads, 0, "no atlas upload happens when both attempts reset");
        expect_eq(grid.dirty_cell_count(), size_t(2), "cells stay dirty so the next flush can retry later");
    });

    run_test("grid rendering pipeline combines two-cell ligatures into a leader and blank continuation", []() {
        Grid grid = make_ligature_grid();
        HighlightTable highlights;
        FakeGlyphAtlas atlas;
        FakeRenderer renderer;
        GridRenderingPipeline pipeline(grid, highlights, atlas);
        pipeline.set_renderer(&renderer);
        pipeline.set_enable_ligatures(true);

        pipeline.flush();

        expect_eq(atlas.resolve_calls, 1, "two-cell ligature should resolve as one combined cluster");
        expect_eq(static_cast<int>(atlas.resolved_texts.size()), 1, "only the combined ligature text should be resolved");
        expect_eq(atlas.resolved_texts[0], std::string("->"), "combined ligature text should be shaped");
        expect_eq(static_cast<int>(renderer.update_batches.size()), 1, "ligature flush emits one update batch");
        expect_eq(static_cast<int>(renderer.update_batches[0].size()), 2, "leader and continuation cells should both be updated");
        expect_eq(renderer.update_batches[0][0].glyph.width, 18, "leader cell stores the ligature atlas region");
        expect_eq(renderer.update_batches[0][1].glyph.width, 0, "continuation cell renders no glyph");
    });

    run_test("grid rendering pipeline redraws the leader when a continuation change breaks a ligature", []() {
        Grid grid = make_ligature_grid();
        HighlightTable highlights;
        FakeGlyphAtlas atlas;
        FakeRenderer renderer;
        GridRenderingPipeline pipeline(grid, highlights, atlas);
        pipeline.set_renderer(&renderer);
        pipeline.set_enable_ligatures(true);

        pipeline.flush();

        atlas.resolve_calls = 0;
        atlas.resolved_texts.clear();
        renderer.update_batches.clear();

        grid.set_cell(1, 0, "X", 0, false);
        pipeline.flush();

        expect_eq(atlas.resolve_calls, 2, "breaking a ligature should redraw both participating cells");
        expect_eq(static_cast<int>(atlas.resolved_texts.size()), 2, "broken ligature should fall back to per-cell shaping");
        expect_eq(atlas.resolved_texts[0], std::string("-"), "leader cell is replayed after the ligature breaks");
        expect_eq(atlas.resolved_texts[1], std::string("X"), "changed continuation cell is shaped normally");
        expect_eq(static_cast<int>(renderer.update_batches.size()), 1, "fallback draw emits one update batch");
        expect_eq(static_cast<int>(renderer.update_batches[0].size()), 2, "both cells are updated to clear the old ligature");
        expect(renderer.update_batches[0][0].glyph.width > 0, "leader redraw restores a standalone glyph");
        expect(renderer.update_batches[0][1].glyph.width > 0, "changed cell restores its standalone glyph");
    });
}
