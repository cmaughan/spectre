#pragma once

#include <draxul/renderer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace draxul
{

// Data sent to the GPU per cell (matches SSBO layout)
struct alignas(16) GpuCell
{
    float pos_x, pos_y; // Screen position in pixels
    float size_x, size_y; // Cell size in pixels
    float bg_r, bg_g, bg_b, bg_a;
    float fg_r, fg_g, fg_b, fg_a;
    float sp_r, sp_g, sp_b, sp_a;
    float uv_x0, uv_y0, uv_x1, uv_y1; // Atlas UVs
    float glyph_offset_x, glyph_offset_y;
    float glyph_size_x, glyph_size_y;
    uint32_t style_flags;
    std::array<uint32_t, 3> _pad{};

    bool operator==(const GpuCell&) const = default;
};
static_assert(sizeof(GpuCell) == 112, "GpuCell must be 112 bytes for SSBO alignment");
// Field offsets must match the shader struct layout in shaders/grid.metal and shaders/grid.glsl
static_assert(offsetof(GpuCell, pos_x) == 0, "GpuCell layout mismatch: pos_x");
static_assert(offsetof(GpuCell, bg_r) == 16, "GpuCell layout mismatch: bg_r");
static_assert(offsetof(GpuCell, fg_r) == 32, "GpuCell layout mismatch: fg_r");
static_assert(offsetof(GpuCell, sp_r) == 48, "GpuCell layout mismatch: sp_r");
static_assert(offsetof(GpuCell, uv_x0) == 64, "GpuCell layout mismatch: uv_x0");
static_assert(offsetof(GpuCell, glyph_offset_x) == 80, "GpuCell layout mismatch: glyph_offset_x");
static_assert(offsetof(GpuCell, glyph_size_x) == 88, "GpuCell layout mismatch: glyph_size_x");
static_assert(offsetof(GpuCell, style_flags) == 96, "GpuCell layout mismatch: style_flags");

class RendererState
{
public:
    static constexpr size_t OVERLAY_CELL_CAPACITY = 256;

    void set_grid_size(int cols, int rows, int padding);
    void set_cell_size(int w, int h);
    void set_ascender(int a);
    void set_default_background(Color bg);

    Color default_bg() const
    {
        return default_bg_;
    }

    void update_cells(std::span<const CellUpdate> updates);
    void set_overlay_cells(std::span<const CellUpdate> updates);
    void set_cursor(int col, int row, const CursorStyle& style);
    void restore_cursor();
    void apply_cursor();

    int grid_cols() const
    {
        return grid_cols_;
    }

    int grid_rows() const
    {
        return grid_rows_;
    }

    int total_cells() const
    {
        return grid_cols_ * grid_rows_;
    }

    int bg_instances() const
    {
        return total_cells() + static_cast<int>(overlay_cell_count_) + (cursor_overlay_active_ ? 1 : 0);
    }

    int fg_instances() const
    {
        return total_cells() + static_cast<int>(overlay_cell_count_);
    }

    size_t buffer_size_bytes() const
    {
        return (gpu_cells_.size() + OVERLAY_CELL_CAPACITY + 1) * sizeof(GpuCell);
    }

    void copy_to(void* dst) const;
    bool has_dirty_cells() const;
    size_t dirty_cell_offset_bytes() const;
    size_t dirty_cell_size_bytes() const;
    void copy_dirty_cells_to(void* dst) const;
    bool overlay_region_dirty() const;
    size_t overlay_offset_bytes() const;
    size_t overlay_region_size_bytes() const;
    void copy_overlay_region_to(void* dst) const;
    void clear_dirty();

private:
    void apply_update_to_cell(GpuCell& cell, const CellUpdate& update);
    void relayout();
    void mark_all_cells_dirty();
    void mark_cell_dirty(size_t index);

    Color default_bg_ = { 0.1f, 0.1f, 0.1f, 1.0f };

    int grid_cols_ = 0;
    int grid_rows_ = 0;
    int cell_w_ = 10;
    int cell_h_ = 20;
    int ascender_ = 16;
    int padding_ = 4;

    int cursor_col_ = 0;
    int cursor_row_ = 0;
    CursorStyle cursor_style_ = {};

    std::vector<GpuCell> gpu_cells_;
    std::vector<GpuCell> overlay_cells_ = std::vector<GpuCell>(OVERLAY_CELL_CAPACITY);
    size_t overlay_cell_count_ = 0;
    GpuCell overlay_cell_ = {};
    GpuCell cursor_saved_cell_ = {};
    bool cursor_applied_ = false;
    bool cursor_overlay_active_ = false;
    size_t dirty_cell_begin_ = 0;
    size_t dirty_cell_end_ = 0;
    bool overlay_dirty_ = false;
};

} // namespace draxul
