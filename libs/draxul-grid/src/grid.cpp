#include <algorithm>
#include <draxul/grid.h>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/unicode.h>
#include <functional>

namespace draxul
{

namespace
{

bool is_utf8_continuation_byte(uint8_t byte)
{
    return (byte & 0xC0) == 0x80;
}

bool utf8_next_codepoint_two_byte(const uint8_t* bytes, size_t offset, size_t remaining, size_t& next_offset)
{
    if (remaining < 2)
        return false;
    if (const uint8_t lead = bytes[offset];
        lead < 0xC2 || lead > 0xDF || !is_utf8_continuation_byte(bytes[offset + 1]))
        return false;
    next_offset = offset + 2;
    return true;
}

bool utf8_next_codepoint_three_byte(const uint8_t* bytes, size_t offset, size_t remaining, size_t& next_offset)
{
    if (remaining < 3)
        return false;

    const uint8_t lead = bytes[offset];
    const uint8_t b1 = bytes[offset + 1];
    const uint8_t b2 = bytes[offset + 2];
    if (const bool valid = (lead == 0xE0 && b1 >= 0xA0 && b1 <= 0xBF && is_utf8_continuation_byte(b2))
            || ((lead >= 0xE1 && lead <= 0xEC) && is_utf8_continuation_byte(b1) && is_utf8_continuation_byte(b2))
            || (lead == 0xED && b1 >= 0x80 && b1 <= 0x9F && is_utf8_continuation_byte(b2))
            || ((lead >= 0xEE && lead <= 0xEF) && is_utf8_continuation_byte(b1) && is_utf8_continuation_byte(b2));
        !valid)
        return false;

    next_offset = offset + 3;
    return true;
}

bool utf8_next_codepoint_four_byte(const uint8_t* bytes, size_t offset, size_t remaining, size_t& next_offset)
{
    if (remaining < 4)
        return false;

    const uint8_t lead = bytes[offset];
    const uint8_t b1 = bytes[offset + 1];
    const uint8_t b2 = bytes[offset + 2];
    const uint8_t b3 = bytes[offset + 3];
    if (const bool valid = (lead == 0xF0 && b1 >= 0x90 && b1 <= 0xBF && is_utf8_continuation_byte(b2)
                               && is_utf8_continuation_byte(b3))
            || ((lead >= 0xF1 && lead <= 0xF3) && is_utf8_continuation_byte(b1) && is_utf8_continuation_byte(b2)
                && is_utf8_continuation_byte(b3))
            || (lead == 0xF4 && b1 >= 0x80 && b1 <= 0x8F && is_utf8_continuation_byte(b2)
                && is_utf8_continuation_byte(b3));
        !valid)
        return false;

    next_offset = offset + 4;
    return true;
}

bool utf8_next_codepoint(std::string_view text, size_t offset, size_t limit, size_t& next_offset)
{
    if (offset >= limit)
        return false;

    const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
    const uint8_t lead = bytes[offset];
    const size_t remaining = limit - offset;

    if (lead < 0x80)
    {
        next_offset = offset + 1;
        return true;
    }

    if (utf8_next_codepoint_two_byte(bytes, offset, remaining, next_offset))
        return true;

    if (utf8_next_codepoint_three_byte(bytes, offset, remaining, next_offset))
        return true;

    if (utf8_next_codepoint_four_byte(bytes, offset, remaining, next_offset))
        return true;

    return false;
}

size_t utf8_valid_prefix_length(std::string_view text, size_t max_len)
{
    const size_t limit = std::min(text.size(), max_len);
    size_t offset = 0;
    size_t valid = 0;

    while (offset < limit)
    {
        size_t next = offset;
        if (!utf8_next_codepoint(text, offset, limit, next))
            break;
        offset = next;
        valid = next;
    }

    return valid;
}

Cell make_blank_cell()
{
    Cell c;
    c.text.assign(" ");
    return c;
}

void clear_continuation(Cell& cell)
{
    cell = Cell{};
    cell.dirty = true;
}

// Helper type for computing flat cell indices from (row, col) pairs.
struct CellIndexer
{
    int grid_cols;

    size_t operator()(int r, int c) const
    {
        return static_cast<size_t>(r) * static_cast<size_t>(grid_cols) + static_cast<size_t>(c);
    }
};

using DirtyFn = std::function<void(int)>;

// Scroll the cells within the region by `delta` rows.
// Positive delta = scroll up (content moves up, new blank rows appear at bottom).
// Negative delta = scroll down (content moves down, new blank rows appear at top).
void scroll_rows(std::vector<Cell>& cells, CellIndexer idx,
    int top, int bot, int left, int right, int delta, const DirtyFn& mark_dirty)
{
    if (delta > 0)
    {
        for (int r = top; r < bot - delta; r++)
        {
            for (int c = left; c < right; c++)
            {
                cells[idx(r, c)] = cells[idx(r + delta, c)];
                mark_dirty(static_cast<int>(idx(r, c)));
            }
        }
        for (int r = bot - delta; r < bot; r++)
        {
            for (int c = left; c < right; c++)
            {
                cells[idx(r, c)] = make_blank_cell();
                mark_dirty(static_cast<int>(idx(r, c)));
            }
        }
    }
    else
    {
        int shift = -delta;
        for (int r = bot - 1; r >= top + shift; r--)
        {
            for (int c = left; c < right; c++)
            {
                cells[idx(r, c)] = cells[idx(r - shift, c)];
                mark_dirty(static_cast<int>(idx(r, c)));
            }
        }
        for (int r = top; r < top + shift; r++)
        {
            for (int c = left; c < right; c++)
            {
                cells[idx(r, c)] = make_blank_cell();
                mark_dirty(static_cast<int>(idx(r, c)));
            }
        }
    }
}

// Scroll the cells within the region by `delta` columns.
// Positive delta = scroll left (content moves left, new blank cols appear at right).
// Negative delta = scroll right (content moves right, new blank cols appear at left).
void scroll_cols(std::vector<Cell>& cells, CellIndexer idx,
    int top, int bot, int left, int right, int delta, const DirtyFn& mark_dirty)
{
    if (delta > 0)
    {
        for (int r = top; r < bot; r++)
        {
            for (int c = left; c < right - delta; c++)
            {
                cells[idx(r, c)] = cells[idx(r, c + delta)];
                mark_dirty(static_cast<int>(idx(r, c)));
            }
            for (int c = right - delta; c < right; c++)
            {
                cells[idx(r, c)] = make_blank_cell();
                mark_dirty(static_cast<int>(idx(r, c)));
            }
        }
    }
    else
    {
        int shift = -delta;
        for (int r = top; r < bot; r++)
        {
            for (int c = right - 1; c >= left + shift; c--)
            {
                cells[idx(r, c)] = cells[idx(r, c - shift)];
                mark_dirty(static_cast<int>(idx(r, c)));
            }
            for (int c = left; c < left + shift; c++)
            {
                cells[idx(r, c)] = make_blank_cell();
                mark_dirty(static_cast<int>(idx(r, c)));
            }
        }
    }
}

} // namespace

void CellText::assign(std::string_view sv)
{
    // TODO: consider std::string for >32-byte clusters instead of truncating.
    if (sv.size() > static_cast<size_t>(kMaxLen))
        DRAXUL_LOG_WARN(LogCategory::App, "CellText: cluster truncated from %zu to %d bytes",
            sv.size(), static_cast<int>(kMaxLen));
    const size_t valid_len = sv.size() > static_cast<size_t>(kMaxLen)
        ? utf8_valid_prefix_length(sv, static_cast<size_t>(kMaxLen))
        : sv.size();
    len = static_cast<uint8_t>(valid_len);
    std::memcpy(data.data(), sv.data(), valid_len);
}

// Maximum dimension for either rows or cols to prevent overflow in index arithmetic.
constexpr int kMaxGridDim = 10000;

void Grid::resize(int cols, int rows)
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("Grid::resize");
    if (cols < 0 || cols > kMaxGridDim || rows < 0 || rows > kMaxGridDim)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Grid::resize: dimensions out of range (cols=%d, rows=%d, max=%d) — clamping",
            cols, rows, kMaxGridDim);
        cols = std::clamp(cols, 0, kMaxGridDim);
        rows = std::clamp(rows, 0, kMaxGridDim);
    }
    cols_ = cols;
    rows_ = rows;
    cells_.resize(static_cast<size_t>(cols) * static_cast<size_t>(rows));
    dirty_marks_.assign(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0);
    dirty_cells_.clear();
    clear();
}

void Grid::clear()
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("Grid::clear");
    for (auto& c : cells_)
        c = make_blank_cell();
    dirty_cells_.clear();
    std::fill(dirty_marks_.begin(), dirty_marks_.end(), (uint8_t)0);
    full_dirty_ = true;
}

void Grid::set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool double_width)
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("Grid::set_cell");
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return;

    const size_t index = static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col);
    if (index >= cells_.size())
        return;
    auto& cell = cells_[index];
    if (col > 0)
    {
        auto& prev = cells_[index - 1];
        if (cell.double_width_cont || prev.double_width)
        {
            prev = make_blank_cell();
            mark_dirty_index(static_cast<int>(index - 1));
        }
    }

    // Clear orphaned continuation flag on the target cell itself (col 0
    // cells skip the prev-cell block above, so the flag can survive resizes).
    cell.double_width_cont = false;

    if (col + 1 < cols_)
    {
        auto& next = cells_[index + 1];
        if (next.double_width_cont)
        {
            clear_continuation(next);
            mark_dirty_index(static_cast<int>(index + 1));
        }
    }

    if (text.size() > CellText::kMaxLen)
        DRAXUL_LOG_WARN(LogCategory::App, "CellText: cluster too long (%zu bytes), truncating to %u bytes",
            text.size(), static_cast<unsigned>(CellText::kMaxLen));
    cell.text.assign(text);
    cell.hl_attr_id = hl_id;
    cell.dirty = false;
    cell.double_width = double_width;
    cell.double_width_cont = false;
    mark_dirty_index(static_cast<int>(index));

    if (double_width && col + 1 < cols_)
    {
        auto& next = cells_[index + 1];
        next.text = CellText{};
        next.hl_attr_id = hl_id;
        next.dirty = false;
        next.double_width = false;
        next.double_width_cont = true;
        mark_dirty_index(static_cast<int>(index + 1));
    }
}

const Cell& Grid::get_cell(int col, int row) const
{
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return empty_cell_;
    return cells_[static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col)];
}

void Grid::scroll(int top, int bot, int left, int right, int rows, int cols)
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("Grid::scroll");
    if (rows == 0 && cols == 0)
        return;

    bool valid = top >= 0 && top < bot && bot <= rows_
        && left >= 0 && left < right && right <= cols_;
    if (!valid)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Grid::scroll received out-of-bounds region: top=%d bot=%d left=%d right=%d grid=%dx%d",
            top, bot, left, right, cols_, rows_);
        return;
    }

    const int region_rows = bot - top;
    const int region_cols = right - left;
    if (rows > region_rows || rows < -region_rows || cols > region_cols || cols < -region_cols)
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "Grid::scroll delta out of range: rows=%d cols=%d region=%dx%d — clamping",
            rows, cols, region_cols, region_rows);
    }
    rows = std::clamp(rows, -region_rows, region_rows);
    cols = std::clamp(cols, -region_cols, region_cols);

    CellIndexer idx{ cols_ };
    auto mark_dirty = [this](int index) { mark_dirty_index(index); };

    if (rows != 0)
        scroll_rows(cells_, idx, top, bot, left, right, rows, mark_dirty);
    if (cols != 0)
        scroll_cols(cells_, idx, top, bot, left, right, cols, mark_dirty);

    // Fix up double-width pairs that were split by the scroll.
    for (int r = top; r < bot; ++r)
    {
        for (int c = left; c < right; ++c)
        {
            const int index = static_cast<int>(idx(r, c));
            auto& cell = cells_[static_cast<size_t>(index)];

            const bool continuation_in_region = c + 1 < right;
            if (cell.double_width
                && (!continuation_in_region || !cells_[static_cast<size_t>(index) + 1].double_width_cont))
            {
                cell = make_blank_cell();
                mark_dirty_index(index);
                continue;
            }

            const bool leader_in_region = c > left;
            if (cell.double_width_cont
                && (!leader_in_region || !cells_[static_cast<size_t>(index) - 1].double_width))
            {
                clear_continuation(cell);
                mark_dirty_index(index);
            }
        }
    }
}

bool Grid::is_dirty(int col, int row) const
{
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return false;
    return cells_[static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col)].dirty;
}

void Grid::mark_dirty(int col, int row)
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("Grid::mark_dirty");
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return;
    mark_dirty_index(static_cast<int>(static_cast<size_t>(row) * static_cast<size_t>(cols_) + static_cast<size_t>(col)));
}

void Grid::mark_all_dirty()
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("Grid::mark_all_dirty");
    dirty_cells_.clear();
    std::fill(dirty_marks_.begin(), dirty_marks_.end(), (uint8_t)0);
    full_dirty_ = true;
}

void Grid::clear_dirty()
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("Grid::clear_dirty");
    for (auto& c : cells_)
        c.dirty = false;
    std::fill(dirty_marks_.begin(), dirty_marks_.end(), (uint8_t)0);
    dirty_cells_.clear();
    full_dirty_ = false;
}

std::vector<Grid::DirtyCell> Grid::get_dirty_cells() const
{
    if (full_dirty_)
    {
        std::vector<DirtyCell> all;
        all.reserve(static_cast<size_t>(cols_) * static_cast<size_t>(rows_));
        for (int r = 0; r < rows_; ++r)
            for (int c = 0; c < cols_; ++c)
                all.push_back({ c, r });
        return all;
    }
    return dirty_cells_;
}

void Grid::mark_dirty_index(int index)
{
    PERF_MEASURE();
    if (index < 0 || index >= (int)cells_.size())
        return;

    auto& cell = cells_[(size_t)index];
    cell.dirty = true;

    if (dirty_marks_[(size_t)index])
        return;

    dirty_marks_[(size_t)index] = 1;
    dirty_cells_.push_back({ index % cols_, index / cols_ });
}

} // namespace draxul
