#pragma once

#include <draxul/grid.h>
#include <draxul/types.h>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace draxul
{

// Manages text-selection state for a terminal grid.
// Receives mouse events from TerminalHostBase and maintains the selection
// anchor/extent.  Notifies the renderer via a set_overlay_cells callback and
// writes selected text to the clipboard via a write_clipboard callback.
class SelectionManager
{
public:
    struct GridPos
    {
        glm::ivec2 pos{ 0 };
    };

    struct Callbacks
    {
        // Push overlay cell updates to the renderer.
        std::function<void(std::vector<CellUpdate>)> set_overlay_cells;
        // Read a cell from the current grid.
        std::function<const Cell&(int col, int row)> get_cell;
        // Return grid dimensions.
        std::function<int()> grid_cols;
        std::function<int()> grid_rows;
        // Request a frame redraw.
        std::function<void()> request_frame;
        // Called when the selection is truncated at the cell limit.
        // The message includes the limit and the total requested cell count.
        std::function<void(std::string_view)> on_selection_truncated;
    };

    explicit SelectionManager(Callbacks cbs, int max_cells = kDefaultSelectionMaxCells);

    bool is_active() const
    {
        return sel_active_;
    }

    int max_cells() const
    {
        return max_cells_;
    }

    // Update the selection-cell cap. Clamped to [kMin, kMax].
    void set_max_cells(int max_cells);

    // Called when the left mouse button is pressed (start drag).
    void begin_drag(GridPos pos);

    // Called when the left mouse button is released (end drag).
    // Returns true if a selection became active.
    bool end_drag(GridPos pos);

    // Called on mouse-move while dragging.
    void update_drag(GridPos pos);

    // Set the selection to the contiguous non-whitespace word containing `pos`.
    // Returns true if a non-empty selection became active.
    bool select_word(GridPos pos);

    // Set the selection to the entire row containing `pos` (trimmed to the
    // last non-space cell). Returns true if a non-empty selection became active.
    bool select_line(GridPos pos);

    // Returns true when `pos` falls inside the current selection bounds.
    bool contains(GridPos pos) const;

    // Clear the selection (overlay removed from renderer).
    void clear();

    // Extract the selected text as a plain string (newline-separated).
    std::string extract_text();

    // Returns true if the most recent extract_text() or update_overlay() hit
    // the cell limit.  Reset to false on clear() or begin_drag().
    bool was_truncated() const
    {
        return was_truncated_;
    }

    // Replace the truncation callback after construction (useful for tests).
    void set_truncation_callback(std::function<void(std::string_view)> cb)
    {
        cbs_.on_selection_truncated = std::move(cb);
    }

    // Default cell cap when no override is supplied. Raised from the historical
    // 8192 (~40 rows at 200 cols) to 65536 (~327 rows at 200 cols) so cross-function
    // selections in code-heavy buffers do not silently truncate. Memory cost at
    // the new default is ~256 KiB of overlay state, which is still trivial.
    static constexpr int kDefaultSelectionMaxCells = 65536;
    static constexpr int kMinSelectionMaxCells = 256;
    static constexpr int kMaxSelectionMaxCells = 1048576;

private:
    void update_overlay();

    Callbacks cbs_;
    int max_cells_ = kDefaultSelectionMaxCells;
    bool sel_active_ = false;
    bool sel_dragging_ = false;
    bool was_truncated_ = false;
    GridPos sel_start_;
    GridPos sel_end_;
};

} // namespace draxul
