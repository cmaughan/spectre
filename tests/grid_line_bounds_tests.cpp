#include "support/replay_fixture.h"
#include "support/test_support.h"

#include <draxul/grid.h>
#include <draxul/log.h>
#include <draxul/nvim.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

// Helper: feed a single redraw event through a UiEventHandler wired to a Grid.
static void feed(UiEventHandler& handler, MpackValue event)
{
    handler.process_redraw({ std::move(event) });
}

TEST_CASE("grid_line with row way out of range does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // row = 9999 — far beyond the 24-row grid
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 9999, 0, { cell("X", 1) }) }));

    // No crash means success. Also verify the grid is not corrupted.
    INFO("grid still has the correct number of columns after OOB row");
    REQUIRE(grid.cols() == 80);
    INFO("grid still has the correct number of rows after OOB row");
    REQUIRE(grid.rows() == 24);
    // The cell at (0,0) should remain the blank the grid was initialized with.
    INFO("OOB row event does not write to any valid cell");
    REQUIRE(grid.get_cell(0, 0).text == std::string(" "));

    // A WARN must have been emitted.
    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for OOB row coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with col_start out of range does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start = 9999 — far beyond the 80-column grid
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, 9999, { cell("X", 1) }) }));

    INFO("grid still has the correct number of columns after OOB col_start");
    REQUIRE(grid.cols() == 80);
    INFO("grid still has the correct number of rows after OOB col_start");
    REQUIRE(grid.rows() == 24);

    // A WARN must have been emitted.
    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for OOB col_start coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with negative row does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // row = -1 — negative value
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, -1, 0, { cell("X", 1) }) }));

    INFO("grid still has the correct dimensions after negative row");
    REQUIRE(grid.cols() == 80);
    REQUIRE(grid.rows() == 24);

    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for negative row coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with negative col_start does not crash", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start = -5 — negative value
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, -5, { cell("X", 1) }) }));

    INFO("grid still has the correct dimensions after negative col_start");
    REQUIRE(grid.cols() == 80);
    REQUIRE(grid.rows() == 24);

    bool found_warn = false;
    for (const auto& rec : cap.records)
    {
        if (rec.level == LogLevel::Warn)
        {
            found_warn = true;
            break;
        }
    }
    INFO("WARN log is emitted for negative col_start coordinate");
    REQUIRE(found_warn);
}

TEST_CASE("grid_line with col_start near max int does not overflow", "[grid_line_bounds]")
{
    ScopedLogCapture cap;

    // Use a large col count to exercise the size_t arithmetic path in set_cell.
    // cols=65536 means the grid holds 65536 * 1 = 65536 cells (1 row).
    Grid grid;
    grid.resize(65536, 1);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    // col_start near INT_MAX — would overflow signed int if used as row*cols+col
    constexpr int kLargeCol = 0x7FFFFFFF; // INT_MAX
    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 0, kLargeCol, { cell("X", 1) }) }));

    // grid dimensions unchanged, no crash.
    INFO("grid dimensions are unchanged after near-INT_MAX col_start");
    REQUIRE(grid.cols() == 65536);
    REQUIRE(grid.rows() == 1);

    // Either a WARN is emitted (if OOB check fires) or the cell is simply
    // silently dropped by set_cell's bounds check — either is acceptable.
    // Most important: no crash and no ASAN error.
}

TEST_CASE("valid grid_line events still work after bounds-checking additions", "[grid_line_bounds]")
{
    Grid grid;
    grid.resize(80, 24);

    HighlightTable highlights;
    UiEventHandler handler;
    handler.set_grid(&grid);
    handler.set_highlights(&highlights);

    feed(handler, redraw_event("grid_line", { grid_line_batch(1, 5, 3, { cell("A", 7), cell("B", 7) }) }));

    INFO("valid cell A lands at the correct position");
    REQUIRE(grid.get_cell(3, 5).text == std::string("A"));
    INFO("valid cell B lands at the correct position");
    REQUIRE(grid.get_cell(4, 5).text == std::string("B"));
}
