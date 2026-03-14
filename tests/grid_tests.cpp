#include "support/test_support.h"

#include <spectre/grid.h>

using namespace spectre;
using namespace spectre::tests;

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
        expect_eq(cell.codepoint, static_cast<uint32_t>('A'), "double-width cell keeps codepoint");
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
                grid.set_cell(col, row, std::string(1, rows[row][col]), 0);
            }
        }

        grid.clear_dirty();
        grid.scroll(0, 3, 0, 3, 1);

        expect_eq(grid.get_cell(0, 0).text, std::string("d"), "row 1 text moves into row 0");
        expect_eq(grid.get_cell(0, 0).codepoint, static_cast<uint32_t>('d'), "row 1 moves into row 0");
        expect_eq(grid.get_cell(2, 1).codepoint, static_cast<uint32_t>('i'), "row 2 moves into row 1");
        expect_eq(grid.get_cell(1, 2).text, std::string(" "), "scrolled-in row text is cleared");
        expect(grid.is_dirty(0, 0), "scroll marks destination cells dirty");
    });
}
