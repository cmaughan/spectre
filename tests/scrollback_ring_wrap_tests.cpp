#include "support/test_support.h"

#include <draxul/scrollback_buffer.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// ScrollbackBuffer ring-buffer wrap-around tests (work item #06)
// ---------------------------------------------------------------------------

namespace
{

constexpr int kTestCols = 10;

// Simple fake grid for scrollback callbacks
struct FakeGrid
{
    int cols = kTestCols;
    int rows = 24;
    std::vector<Cell> cells;

    FakeGrid()
    {
        cells.resize(static_cast<size_t>(cols) * rows);
    }

    ScrollbackBuffer::Callbacks make_callbacks()
    {
        return {
            .grid_cols = [this]() { return cols; },
            .grid_rows = [this]() { return rows; },
            .get_cell = [this](int c, int r) -> Cell { return cells[static_cast<size_t>(r) * cols + c]; },
            .set_cell = [this](int c, int r, const Cell& cell) { cells[static_cast<size_t>(r) * cols + c] = cell; },
            .force_full_redraw = []() {},
            .flush_grid = []() {},
        };
    }
};

void push_row(ScrollbackBuffer& sb, char fill_char)
{
    Cell* slot = sb.next_write_slot();
    REQUIRE(slot != nullptr);
    for (int c = 0; c < sb.cols(); c++)
    {
        slot[c].text.assign(std::string(1, fill_char));
        slot[c].hl_attr_id = 0;
    }
    sb.commit_push();
}

} // namespace

TEST_CASE("scrollback: push within capacity stores all rows", "[scrollback][ring]")
{
    FakeGrid grid;
    ScrollbackBuffer sb(grid.make_callbacks());
    sb.resize(kTestCols);

    push_row(sb, 'A');
    push_row(sb, 'B');
    push_row(sb, 'C');

    REQUIRE(sb.size() == 3);
}

TEST_CASE("scrollback: push beyond capacity evicts oldest rows", "[scrollback][ring]")
{
    FakeGrid grid;
    ScrollbackBuffer sb(grid.make_callbacks());
    sb.resize(kTestCols);

    // Push more rows than capacity
    for (int i = 0; i < ScrollbackBuffer::kCapacity + 100; i++)
        push_row(sb, static_cast<char>('A' + (i % 26)));

    REQUIRE(sb.size() == ScrollbackBuffer::kCapacity);
}

TEST_CASE("scrollback: row content is correct after wrap-around", "[scrollback][ring]")
{
    FakeGrid grid;
    ScrollbackBuffer sb(grid.make_callbacks());
    sb.resize(kTestCols);

    // Push exactly capacity + 5 rows
    const int total = ScrollbackBuffer::kCapacity + 5;
    for (int i = 0; i < total; i++)
        push_row(sb, static_cast<char>('A' + (i % 26)));

    REQUIRE(sb.size() == ScrollbackBuffer::kCapacity);

    // Verify content via for_each_cell: the first row should correspond
    // to the 6th push (index 5), which is 'F' (5 % 26 = 5)
    int cell_count = 0;
    sb.for_each_cell([&](const Cell& cell) {
        if (cell_count < kTestCols)
        {
            // First stored row = push index 5 → 'F'
            REQUIRE(cell.text.view() == std::string(1, 'F'));
        }
        cell_count++;
    });

    REQUIRE(cell_count == ScrollbackBuffer::kCapacity * kTestCols);
}

TEST_CASE("scrollback: viewport offset is clamped to stored rows", "[scrollback][ring]")
{
    FakeGrid grid;
    ScrollbackBuffer sb(grid.make_callbacks());
    sb.resize(kTestCols);

    push_row(sb, 'A');
    push_row(sb, 'B');
    push_row(sb, 'C');

    // Scroll way beyond stored count
    sb.scroll(1000);
    REQUIRE(sb.offset() <= sb.size());

    // Scroll back past zero
    sb.scroll(-5000);
    REQUIRE(sb.offset() == 0);
    REQUIRE_FALSE(sb.is_scrolled_back());
}

TEST_CASE("scrollback: resize preserves rows and resets offset", "[scrollback][ring]")
{
    // WI 98: resize() preserves stored rows so horizontal terminal resizes do
    // not erase the user's scrollback history. Columns are clamped or extended
    // and the viewport offset is reset to 0.
    FakeGrid grid;
    ScrollbackBuffer sb(grid.make_callbacks());
    sb.resize(kTestCols);

    push_row(sb, 'A');
    push_row(sb, 'B');
    REQUIRE(sb.size() == 2);

    sb.resize(kTestCols + 5);
    REQUIRE(sb.size() == 2);
    REQUIRE(sb.offset() == 0);
    REQUIRE(sb.cols() == kTestCols + 5);
}

TEST_CASE("scrollback: reset clears rows but preserves column count", "[scrollback][ring]")
{
    FakeGrid grid;
    ScrollbackBuffer sb(grid.make_callbacks());
    sb.resize(kTestCols);

    push_row(sb, 'A');
    push_row(sb, 'B');
    push_row(sb, 'C');

    sb.reset();
    REQUIRE(sb.size() == 0);
    REQUIRE(sb.offset() == 0);
    REQUIRE(sb.cols() == kTestCols);
}

TEST_CASE("scrollback: next_write_slot returns nullptr before resize", "[scrollback][ring]")
{
    FakeGrid grid;
    ScrollbackBuffer sb(grid.make_callbacks());

    REQUIRE(sb.next_write_slot() == nullptr);
}
