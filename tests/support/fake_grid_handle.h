#pragma once

// WI 25 — canonical FakeGridHandle for tests.
//
// Minimal in-memory IGridHandle that records every cell-update batch, overlay
// assignment, cursor move, grid size, and viewport change so tests can assert
// on what a host pushed to the renderer without spinning up a real GPU
// pipeline. Matches the recording pattern used by FakeGridPipelineHandle but
// lives in its own header so tests that only need a grid handle do not need
// to drag in the full renderer fake.

#include <draxul/renderer.h>
#include <draxul/types.h>

#include <glm/glm.hpp>

#include <span>
#include <vector>

namespace draxul::tests
{

class FakeGridHandle final : public IGridHandle
{
public:
    void set_grid_size(int cols, int rows) override
    {
        last_grid_size = { cols, rows };
        ++set_grid_size_calls;
    }

    void update_cells(std::span<const CellUpdate> updates) override
    {
        update_batches.emplace_back(updates.begin(), updates.end());
    }

    void set_overlay_cells(std::span<const CellUpdate> updates) override
    {
        last_overlay.assign(updates.begin(), updates.end());
        ++set_overlay_calls;
    }

    void set_cursor(int col, int row, const CursorStyle&) override
    {
        last_cursor = { col, row };
        ++set_cursor_calls;
    }

    void set_cursor_visible(bool visible) override
    {
        last_cursor_visible = visible;
    }

    void set_default_background(Color color) override
    {
        last_default_background = color;
    }

    void set_scroll_offset(float offset) override
    {
        last_scroll_offset = offset;
    }

    void set_viewport(const PaneDescriptor& desc) override
    {
        last_viewport = desc;
    }

    size_t total_cell_updates() const
    {
        size_t total = 0;
        for (const auto& batch : update_batches)
            total += batch.size();
        return total;
    }

    void reset()
    {
        update_batches.clear();
        last_overlay.clear();
        last_grid_size = { 0, 0 };
        last_viewport = {};
        last_cursor = { 0, 0 };
        last_cursor_visible = true;
        last_default_background = Color{};
        last_scroll_offset = 0.0f;
        set_grid_size_calls = 0;
        set_overlay_calls = 0;
        set_cursor_calls = 0;
    }

    // Recorded state — read by tests.
    std::vector<std::vector<CellUpdate>> update_batches;
    std::vector<CellUpdate> last_overlay;
    glm::ivec2 last_grid_size{ 0, 0 };
    PaneDescriptor last_viewport{};
    glm::ivec2 last_cursor{ 0, 0 };
    bool last_cursor_visible = true;
    Color last_default_background{};
    float last_scroll_offset = 0.0f;

    // Call counts.
    int set_grid_size_calls = 0;
    int set_overlay_calls = 0;
    int set_cursor_calls = 0;
};

} // namespace draxul::tests
