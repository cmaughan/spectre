#include <algorithm>
#include <spectre/grid.h>
#include <spectre/unicode.h>

namespace spectre
{

namespace
{

Cell make_blank_cell()
{
    return Cell{};
}

void clear_continuation(Cell& cell)
{
    cell = make_blank_cell();
    cell.text.clear();
    cell.codepoint = ' ';
    cell.dirty = true;
}

} // namespace

void Grid::resize(int cols, int rows)
{
    cols_ = cols;
    rows_ = rows;
    cells_.resize(cols * rows);
    clear();
}

void Grid::clear()
{
    for (auto& c : cells_)
    {
        c = make_blank_cell();
    }
}

void Grid::set_cell(int col, int row, const std::string& text, uint16_t hl_id, bool double_width)
{
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return;

    auto& cell = cells_[row * cols_ + col];
    if (col + 1 < cols_)
    {
        auto& next = cells_[row * cols_ + col + 1];
        if (next.double_width_cont)
            clear_continuation(next);
    }

    cell.text = text;
    cell.codepoint = utf8_first_codepoint(text);
    cell.hl_attr_id = hl_id;
    cell.dirty = true;
    cell.double_width = double_width;
    cell.double_width_cont = false;

    if (double_width && col + 1 < cols_)
    {
        auto& next = cells_[row * cols_ + col + 1];
        next.text.clear();
        next.codepoint = ' ';
        next.hl_attr_id = hl_id;
        next.dirty = true;
        next.double_width = false;
        next.double_width_cont = true;
    }
}

const Cell& Grid::get_cell(int col, int row) const
{
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return empty_cell_;
    return cells_[row * cols_ + col];
}

void Grid::scroll(int top, int bot, int left, int right, int rows)
{
    if (rows == 0)
        return;

    if (rows > 0)
    {
        for (int r = top; r < bot - rows; r++)
        {
            for (int c = left; c < right; c++)
            {
                cells_[r * cols_ + c] = cells_[(r + rows) * cols_ + c];
                cells_[r * cols_ + c].dirty = true;
            }
        }
        for (int r = bot - rows; r < bot; r++)
        {
            for (int c = left; c < right; c++)
            {
                cells_[r * cols_ + c] = make_blank_cell();
            }
        }
    }
    else
    {
        int shift = -rows;
        for (int r = bot - 1; r >= top + shift; r--)
        {
            for (int c = left; c < right; c++)
            {
                cells_[r * cols_ + c] = cells_[(r - shift) * cols_ + c];
                cells_[r * cols_ + c].dirty = true;
            }
        }
        for (int r = top; r < top + shift; r++)
        {
            for (int c = left; c < right; c++)
            {
                cells_[r * cols_ + c] = make_blank_cell();
            }
        }
    }

    for (int r = top; r < bot; r++)
    {
        for (int c = left; c < right; c++)
        {
            cells_[r * cols_ + c].dirty = true;
        }
    }
}

bool Grid::is_dirty(int col, int row) const
{
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return false;
    return cells_[row * cols_ + col].dirty;
}

void Grid::mark_dirty(int col, int row)
{
    if (col < 0 || col >= cols_ || row < 0 || row >= rows_)
        return;
    cells_[row * cols_ + col].dirty = true;
}

void Grid::mark_all_dirty()
{
    for (auto& c : cells_)
        c.dirty = true;
}

void Grid::clear_dirty()
{
    for (auto& c : cells_)
        c.dirty = false;
}

std::vector<Grid::DirtyCell> Grid::get_dirty_cells() const
{
    std::vector<DirtyCell> result;
    for (int r = 0; r < rows_; r++)
    {
        for (int c = 0; c < cols_; c++)
        {
            if (cells_[r * cols_ + c].dirty)
            {
                result.push_back({ c, r });
            }
        }
    }
    return result;
}

} // namespace spectre
