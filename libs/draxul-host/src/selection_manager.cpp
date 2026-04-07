#include <draxul/selection_manager.h>

#include <algorithm>
#include <cstdio>
#include <draxul/log.h>
#include <draxul/perf_timing.h>

namespace draxul
{

SelectionManager::SelectionManager(Callbacks cbs, int max_cells)
    : cbs_(std::move(cbs))
    , max_cells_(std::clamp(max_cells, kMinSelectionMaxCells, kMaxSelectionMaxCells))
{
}

void SelectionManager::set_max_cells(int max_cells)
{
    max_cells_ = std::clamp(max_cells, kMinSelectionMaxCells, kMaxSelectionMaxCells);
}

void SelectionManager::begin_drag(GridPos pos)
{
    PERF_MEASURE();
    clear();
    was_truncated_ = false;
    sel_dragging_ = true;
    sel_start_ = pos;
    sel_end_ = pos;
}

bool SelectionManager::end_drag(GridPos pos)
{
    PERF_MEASURE();
    sel_end_ = pos;
    sel_dragging_ = false;
    if (sel_start_.pos.x != sel_end_.pos.x || sel_start_.pos.y != sel_end_.pos.y)
    {
        sel_active_ = true;
        update_overlay();
        return true;
    }
    return false;
}

void SelectionManager::update_drag(GridPos pos)
{
    PERF_MEASURE();
    if (!sel_dragging_)
        return;
    sel_end_ = pos;
    sel_active_ = (sel_start_.pos.x != sel_end_.pos.x || sel_start_.pos.y != sel_end_.pos.y);
    if (sel_active_)
        update_overlay();
}

namespace
{
bool is_word_char(const Cell& cell)
{
    const auto sv = cell.text.view();
    if (sv.empty())
        return false;
    if (sv.size() == 1)
    {
        const unsigned char c = static_cast<unsigned char>(sv[0]);
        return c > ' ' && c != 0x7F;
    }
    // Multi-byte glyphs (UTF-8) are treated as word characters; only ASCII
    // whitespace and control codes break a word.
    return true;
}
} // namespace

bool SelectionManager::select_word(GridPos pos)
{
    PERF_MEASURE();
    clear();
    was_truncated_ = false;
    sel_dragging_ = false;

    const int cols = cbs_.grid_cols();
    const int rows = cbs_.grid_rows();
    const int row = pos.pos.y;
    const int col = pos.pos.x;
    if (row < 0 || row >= rows || col < 0 || col >= cols)
        return false;

    const auto& clicked = cbs_.get_cell(col, row);
    if (!is_word_char(clicked))
        return false;

    int start_col = col;
    while (start_col > 0 && is_word_char(cbs_.get_cell(start_col - 1, row)))
        --start_col;
    int end_col = col;
    while (end_col + 1 < cols && is_word_char(cbs_.get_cell(end_col + 1, row)))
        ++end_col;

    sel_start_ = { { start_col, row } };
    sel_end_ = { { end_col, row } };
    sel_active_ = true;
    update_overlay();
    return true;
}

bool SelectionManager::select_line(GridPos pos)
{
    PERF_MEASURE();
    clear();
    was_truncated_ = false;
    sel_dragging_ = false;

    const int cols = cbs_.grid_cols();
    const int rows = cbs_.grid_rows();
    const int row = pos.pos.y;
    if (row < 0 || row >= rows || cols <= 0)
        return false;

    // Trim trailing spaces by walking back from the right edge.
    int last = cols - 1;
    while (last > 0)
    {
        const auto sv = cbs_.get_cell(last, row).text.view();
        if (!sv.empty() && sv != " ")
            break;
        --last;
    }

    sel_start_ = { { 0, row } };
    sel_end_ = { { last, row } };
    sel_active_ = true;
    update_overlay();
    return true;
}

void SelectionManager::clear()
{
    PERF_MEASURE();
    const bool was_active = sel_active_;
    sel_active_ = false;
    sel_dragging_ = false;
    cbs_.set_overlay_cells({});
    if (was_active)
        cbs_.request_frame();
}

std::string SelectionManager::extract_text()
{
    PERF_MEASURE();
    if (!sel_active_)
        return {};

    int r1 = sel_start_.pos.y, c1 = sel_start_.pos.x;
    int r2 = sel_end_.pos.y, c2 = sel_end_.pos.x;
    if (r1 > r2 || (r1 == r2 && c1 > c2))
    {
        std::swap(r1, r2);
        std::swap(c1, c2);
    }

    // Compute total cells in the requested selection to detect truncation.
    const int cols = cbs_.grid_cols();
    int total_requested = 0;
    for (int row = r1; row <= r2; ++row)
    {
        const int sc = (row == r1) ? c1 : 0;
        const int ec = (row == r2) ? c2 : cols - 1;
        total_requested += (ec - sc + 1);
    }

    std::string result;
    int cell_count = 0;
    for (int row = r1; row <= r2 && cell_count < max_cells_; ++row)
    {
        const int sc = (row == r1) ? c1 : 0;
        const int ec = (row == r2) ? c2 : cols - 1;
        const size_t line_start = result.size();
        for (int col = sc; col <= ec && cell_count < max_cells_; ++col)
        {
            const auto& cell = cbs_.get_cell(col, row);
            if (!cell.double_width_cont)
                result += cell.text.view();
            ++cell_count;
        }
        if (row < r2 && cell_count < max_cells_)
        {
            // Trim trailing spaces from this line before the newline.
            while (result.size() > line_start && result.back() == ' ')
                result.pop_back();
            result += '\n';
        }
    }
    while (!result.empty() && result.back() == ' ')
        result.pop_back();

    if (total_requested > max_cells_)
    {
        was_truncated_ = true;
        DRAXUL_LOG_WARN(LogCategory::App,
            "selection truncated during copy: %d of %d cells copied (limit %d)",
            cell_count, total_requested, max_cells_);
        if (cbs_.on_selection_truncated)
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "Selection truncated: copied %d of %d cells (limit %d)",
                cell_count, total_requested, max_cells_);
            cbs_.on_selection_truncated(msg);
        }
    }

    return result;
}

void SelectionManager::update_overlay()
{
    PERF_MEASURE();
    if (!sel_active_)
    {
        cbs_.set_overlay_cells({});
        return;
    }

    int r1 = sel_start_.pos.y, c1 = sel_start_.pos.x;
    int r2 = sel_end_.pos.y, c2 = sel_end_.pos.x;
    if (r1 > r2 || (r1 == r2 && c1 > c2))
    {
        std::swap(r1, r2);
        std::swap(c1, c2);
    }

    static const Color kSelBg(0.27f, 0.44f, 0.78f, 1.0f);
    static const Color kSelFg(1.0f, 1.0f, 1.0f, 1.0f);

    const int cols = cbs_.grid_cols();

    // Compute total cells in the requested selection to detect truncation.
    int total_requested = 0;
    for (int row = r1; row <= r2; ++row)
    {
        const int sc = (row == r1) ? c1 : 0;
        const int ec = (row == r2) ? c2 : cols - 1;
        total_requested += (ec - sc + 1);
    }

    std::vector<CellUpdate> overlays;
    overlays.reserve(std::min(max_cells_, (r2 - r1 + 1) * cols));
    for (int row = r1; row <= r2 && static_cast<int>(overlays.size()) < max_cells_; ++row)
    {
        const int sc = (row == r1) ? c1 : 0;
        const int ec = (row == r2) ? c2 : cols - 1;
        for (int col = sc; col <= ec && static_cast<int>(overlays.size()) < max_cells_; ++col)
        {
            CellUpdate cu;
            cu.col = col;
            cu.row = row;
            cu.bg = kSelBg;
            cu.fg = kSelFg;
            overlays.push_back(cu);
        }
    }
    if (total_requested > max_cells_)
    {
        was_truncated_ = true;
        DRAXUL_LOG_WARN(LogCategory::App,
            "selection overlay truncated: %d of %d cells shown (limit %d)",
            static_cast<int>(overlays.size()), total_requested, max_cells_);
        if (cbs_.on_selection_truncated)
        {
            char msg[256];
            std::snprintf(msg, sizeof(msg),
                "Selection truncated: showing %d of %d cells (limit %d)",
                static_cast<int>(overlays.size()), total_requested, max_cells_);
            cbs_.on_selection_truncated(msg);
        }
    }

    cbs_.set_overlay_cells(std::move(overlays));
    cbs_.request_frame();
}

} // namespace draxul
