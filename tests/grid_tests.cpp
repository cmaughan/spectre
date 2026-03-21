#include "support/test_support.h"

#include <draxul/grid.h>
#include <draxul/log.h>
#include <draxul/unicode.h>

using namespace draxul;
using namespace draxul::tests;

void run_grid_tests()
{
    run_test("grid tracks double-width continuations", []() {
        Grid grid;
        grid.resize(4, 2);
        grid.clear_dirty();
        grid.set_cell(1, 0, "A", 7, true);

        const auto& cell = grid.get_cell(1, 0);
        const auto& cont = grid.get_cell(2, 0);
        expect_eq(cell.text, std::string("A"), "double-width cell keeps text");
        expect_eq(utf8_first_codepoint(cell.text.view()), static_cast<uint32_t>('A'), "double-width cell keeps codepoint");
        expect(cell.double_width, "double-width flag is set");
        expect(cont.double_width_cont, "continuation cell is marked");
        expect_eq(cont.hl_attr_id, static_cast<uint16_t>(7), "continuation cell carries highlight");
    });

    run_test("grid scroll shifts rows and blanks the tail", []() {
        Grid grid;
        grid.resize(3, 3);

        const char rows[3][3] = {
            { 'a', 'b', 'c' },
            { 'd', 'e', 'f' },
            { 'g', 'h', 'i' },
        };

        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                grid.set_cell(col, row, std::string(1, rows[row][col]), 0, false);
            }
        }

        grid.clear_dirty();
        grid.scroll(0, 3, 0, 3, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("d"), "row 1 text moves into row 0");
        expect_eq(utf8_first_codepoint(grid.get_cell(0, 0).text.view()), static_cast<uint32_t>('d'), "row 1 moves into row 0");
        expect_eq(utf8_first_codepoint(grid.get_cell(2, 1).text.view()), static_cast<uint32_t>('i'), "row 2 moves into row 1");
        expect_eq(grid.get_cell(1, 2).text, std::string(" "), "scrolled-in row text is cleared");
        expect(grid.is_dirty(0, 0), "scroll marks destination cells dirty");
    });

    run_test("grid clears stale continuations when overwriting double-width cells", []() {
        Grid grid;
        grid.resize(4, 1);

        grid.set_cell(1, 0, "X", 3, true);
        grid.set_cell(1, 0, "Y", 4, false);

        expect_eq(grid.get_cell(1, 0).text, std::string("Y"), "overwriting cell replaces text");
        expect_eq(grid.get_cell(1, 0).double_width, false, "overwriting cell clears double-width flag");
        expect_eq(grid.get_cell(2, 0).text, std::string(), "continuation cell text is cleared");
        expect_eq(grid.get_cell(2, 0).double_width_cont, false, "continuation marker is cleared");
    });

    run_test("grid clears stale leaders when overwriting a continuation cell", []() {
        Grid grid;
        grid.resize(4, 1);

        grid.set_cell(1, 0, "X", 3, true);
        grid.set_cell(2, 0, "Y", 4, false);

        expect_eq(grid.get_cell(1, 0).text, std::string(" "), "former leader is cleared when its continuation is overwritten");
        expect_eq(grid.get_cell(1, 0).double_width, false, "former leader clears the double-width flag");
        expect_eq(grid.get_cell(2, 0).text, std::string("Y"), "replacement text lands in the overwritten continuation column");
        expect_eq(grid.get_cell(2, 0).double_width_cont, false, "replacement cell is no longer marked as a continuation");
    });

    run_test("grid scroll preserves double-width cells and continuations together", []() {
        Grid grid;
        grid.resize(4, 2);
        grid.set_cell(0, 1, "Z", 9, true);
        grid.clear_dirty();

        grid.scroll(0, 2, 0, 4, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("Z"), "double-width source cell scrolls into destination row");
        expect_eq(grid.get_cell(0, 0).double_width, true, "double-width flag scrolls with the cell");
        expect(grid.get_cell(1, 0).double_width_cont, "continuation cell scrolls with the source cell");
        expect_eq(grid.get_cell(0, 1).text, std::string(" "), "vacated row is cleared after scroll");
    });

    run_test("grid scroll supports horizontal shifts within a partial region", []() {
        Grid grid;
        grid.resize(5, 1);
        grid.set_cell(0, 0, "a", 1, false);
        grid.set_cell(1, 0, "b", 1, false);
        grid.set_cell(2, 0, "c", 1, false);
        grid.set_cell(3, 0, "d", 1, false);
        grid.set_cell(4, 0, "e", 1, false);
        grid.clear_dirty();

        grid.scroll(0, 1, 1, 5, 0, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("a"), "cells outside the scrolled region stay unchanged");
        expect_eq(grid.get_cell(1, 0).text, std::string("c"), "horizontal scroll shifts region content left");
        expect_eq(grid.get_cell(2, 0).text, std::string("d"), "middle cells shift left");
        expect_eq(grid.get_cell(3, 0).text, std::string("e"), "tail cell shifts left");
        expect_eq(grid.get_cell(4, 0).text, std::string(" "), "newly uncovered cell is cleared");
    });

    run_test("grid scroll preserves wide pairs fully inside a partial region", []() {
        Grid grid;
        grid.resize(6, 1);
        grid.set_cell(0, 0, "a", 1, false);
        grid.set_cell(1, 0, "b", 1, false);
        grid.set_cell(2, 0, "W", 7, true);
        grid.set_cell(4, 0, "c", 1, false);
        grid.set_cell(5, 0, "z", 1, false);
        grid.clear_dirty();

        grid.scroll(0, 1, 1, 5, 0, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("a"), "outside columns before the region stay unchanged");
        expect_eq(grid.get_cell(1, 0).text, std::string("W"), "wide leader shifts into the partial region");
        expect(grid.get_cell(1, 0).double_width, "wide leader keeps its flag when the pair stays in-region");
        expect(grid.get_cell(2, 0).double_width_cont, "continuation shifts with the leader when fully in-region");
        expect_eq(grid.get_cell(3, 0).text, std::string("c"), "trailing in-region text still shifts left");
        expect_eq(grid.get_cell(5, 0).text, std::string("z"), "outside columns after the region stay unchanged");
    });

    run_test("grid scroll clears orphaned continuations at the left boundary without touching outside columns", []() {
        Grid grid;
        grid.resize(5, 2);
        grid.set_cell(0, 0, "x", 1, false);
        grid.set_cell(1, 0, "y", 1, false);
        grid.set_cell(2, 0, "z", 1, false);
        grid.set_cell(3, 0, "u", 1, false);
        grid.set_cell(4, 0, "v", 1, false);
        grid.set_cell(0, 1, "W", 7, true);
        grid.set_cell(2, 1, "a", 1, false);
        grid.set_cell(3, 1, "b", 1, false);
        grid.set_cell(4, 1, "c", 1, false);
        grid.clear_dirty();

        grid.scroll(0, 2, 1, 5, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("x"), "column left of the region is not clobbered");
        expect_eq(grid.get_cell(1, 0).text, std::string(), "orphaned continuation at the left boundary is cleared");
        expect_eq(grid.get_cell(1, 0).double_width_cont, false, "cleared continuation resets the continuation flag");
        expect_eq(grid.get_cell(2, 0).text, std::string("a"), "in-region cells still scroll vertically");
        expect_eq(grid.get_cell(3, 0).text, std::string("b"), "middle cells keep their scrolled value");
        expect_eq(grid.get_cell(4, 0).text, std::string("c"), "tail cells keep their scrolled value");
    });

    run_test("grid scroll does not clobber wide leaders outside a partial region", []() {
        Grid grid;
        grid.resize(5, 1);
        grid.set_cell(0, 0, "W", 7, true);
        grid.set_cell(2, 0, "a", 1, false);
        grid.set_cell(3, 0, "b", 1, false);
        grid.set_cell(4, 0, "c", 1, false);
        grid.clear_dirty();

        grid.scroll(0, 1, 1, 5, 0, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("W"), "leader outside the region stays untouched");
        expect(grid.get_cell(0, 0).double_width, "outside-region leader keeps its wide flag");
        expect_eq(grid.get_cell(1, 0).text, std::string("a"), "scrolled text stays aligned after boundary repair");
        expect_eq(grid.get_cell(2, 0).text, std::string("b"), "middle cells continue to shift left");
        expect_eq(grid.get_cell(3, 0).text, std::string("c"), "tail cells continue to shift left");
    });

    run_test("grid scroll clears leaders at the right boundary without touching outside continuations", []() {
        Grid grid;
        grid.resize(5, 1);
        grid.set_cell(0, 0, "a", 1, false);
        grid.set_cell(1, 0, "b", 1, false);
        grid.set_cell(2, 0, "c", 1, false);
        grid.set_cell(3, 0, "W", 7, true);
        grid.clear_dirty();

        grid.scroll(0, 1, 0, 4, 0, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("b"), "scrolled prefix still shifts left");
        expect_eq(grid.get_cell(1, 0).text, std::string("c"), "middle cells keep their shifted value");
        expect_eq(grid.get_cell(2, 0).text, std::string(" "), "orphaned leader is cleared after the shift");
        expect_eq(grid.get_cell(2, 0).double_width, false, "cleared leader resets the double-width flag");
        expect(grid.get_cell(4, 0).double_width_cont, "continuation outside the region is left untouched");
    });

    run_test("grid scroll full-width repair clears orphaned continuations", []() {
        Grid grid;
        grid.resize(5, 1);
        grid.set_cell(0, 0, "W", 7, true);
        grid.set_cell(2, 0, "a", 1, false);
        grid.set_cell(3, 0, "b", 1, false);
        grid.set_cell(4, 0, "c", 1, false);
        grid.clear_dirty();

        grid.scroll(0, 1, 0, 5, 0, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string(), "full-width repair clears continuations orphaned at column zero");
        expect_eq(grid.get_cell(0, 0).double_width_cont, false, "cleared continuation resets the continuation flag");
        expect_eq(grid.get_cell(1, 0).text, std::string("a"), "remaining cells still shift left");
        expect_eq(grid.get_cell(2, 0).text, std::string("b"), "middle cells keep their shifted value");
        expect_eq(grid.get_cell(3, 0).text, std::string("c"), "tail cells keep their shifted value");
    });

    run_test("grid ignores double-width continuation when placed at the right edge", []() {
        Grid grid;
        grid.resize(2, 1);

        grid.set_cell(1, 0, "X", 7, true);

        expect_eq(grid.get_cell(1, 0).text, std::string("X"), "edge cell text is written");
        expect_eq(grid.get_cell(1, 0).double_width, true, "edge cell keeps the double-width flag");
        expect_eq(grid.get_cell(0, 0).text, std::string(" "), "neighboring cells are untouched");
    });

    run_test("set_cell warns when cluster exceeds CellText::kMaxLen", []() {
        ScopedLogCapture cap;
        std::string long_cluster(40, 'x');
        Grid grid;
        grid.resize(2, 1);
        grid.set_cell(0, 0, long_cluster, 0, false);
        bool found_warn = false;
        for (const auto& rec : cap.records)
        {
            if (rec.level == LogLevel::Warn && std::string_view(rec.message).find("truncat") != std::string_view::npos)
            {
                found_warn = true;
                break;
            }
        }
        expect(found_warn, "expected a Warn log containing 'truncat' when cluster exceeds kMaxLen");
    });

    run_test("grid clear resets dirty bookkeeping for later cell updates", []() {
        Grid grid;
        grid.resize(4, 2);
        grid.clear_dirty();

        grid.clear();
        grid.set_cell(0, 0, "X", 1, false);

        auto dirty = grid.get_dirty_cells();
        expect(!dirty.empty(), "dirty list is repopulated after clear");

        bool saw_origin = false;
        for (const auto& cell : dirty)
        {
            if (cell.col == 0 && cell.row == 0)
            {
                saw_origin = true;
                break;
            }
        }

        expect(saw_origin, "updated origin cell is present in dirty list");
        expect_eq(grid.get_cell(0, 0).text, std::string("X"), "updated cell text is preserved");
    });
}
