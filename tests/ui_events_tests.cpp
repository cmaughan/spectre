#include "support/replay_fixture.h"
#include "support/test_support.h"

#include <spectre/grid.h>
#include <spectre/nvim.h>

using namespace spectre;
using namespace spectre::tests;

void run_ui_events_tests()
{
    run_test("ui event handler replays redraw batches into the grid", []() {
        Grid grid;
        grid.resize(4, 2);

        HighlightTable highlights;
        UiEventHandler handler;
        handler.set_grid(&grid);
        handler.set_highlights(&highlights);

        int flushes = 0;
        handler.on_flush = [&]() { ++flushes; };

        handler.process_redraw({ redraw_event("default_colors_set", { arr({ i(0xFFFFFF), i(0x101010), i(0xABCDEF) }) }),
            redraw_event("hl_attr_define", { arr({ i(7), map({ { s("foreground"), i(0x123456) }, { s("background"), i(0x654321) }, { s("bold"), b(true) } }) }) }),
            redraw_event("grid_line", { grid_line_batch(1, 0, 0, { cell("A", 7), cell("B", std::nullopt, 2) }) }),
            redraw_event("grid_cursor_goto", { arr({ i(1), i(1), i(2) }) }),
            redraw_event("mode_info_set", { arr({ b(true), arr({ map({ { s("name"), s("normal") }, { s("cursor_shape"), s("vertical") }, { s("cell_percentage"), i(25) }, { s("attr_id"), i(7) } }) }) }) }),
            redraw_event("mode_change", { arr({ s("normal"), i(0) }) }),
            redraw_event("flush", {}) });

        expect_eq(flushes, 1, "flush callback fires once");
        expect_eq(grid.get_cell(0, 0).text, std::string("A"), "first cell text is preserved");
        expect_eq(grid.get_cell(0, 0).codepoint, static_cast<uint32_t>('A'), "first cell is written");
        expect_eq(grid.get_cell(1, 0).codepoint, static_cast<uint32_t>('B'), "repeat cell writes first copy");
        expect_eq(grid.get_cell(2, 0).codepoint, static_cast<uint32_t>('B'), "repeat cell writes second copy");
        expect_eq(handler.cursor_row(), 1, "cursor row is updated");
        expect_eq(handler.cursor_col(), 2, "cursor col is updated");
        expect_eq(handler.current_mode(), 0, "mode index is updated");
        expect_eq(static_cast<int>(handler.modes().size()), 1, "mode table is populated");
        expect_eq(handler.modes()[0].attr_id, 7, "mode attr id is preserved");
        expect_eq(handler.modes()[0].cell_percentage, 25, "mode cell percentage is preserved");
        expect_eq(highlights.get(7).bold, true, "highlight attributes are stored");
        expect_eq(highlights.default_bg(), Color::from_rgb(0x101010), "default background is updated");
    });

    run_test("ui event handler forwards grid resize callbacks", []() {
        Grid grid;
        grid.resize(2, 2);

        HighlightTable highlights;
        UiEventHandler handler;
        handler.set_grid(&grid);
        handler.set_highlights(&highlights);

        int resized_cols = 0;
        int resized_rows = 0;
        handler.on_grid_resize = [&](int cols, int rows) {
            resized_cols = cols;
            resized_rows = rows;
        };

        handler.process_redraw({ redraw_event("grid_resize", { arr({ i(1), i(6), i(4) }) }) });

        expect_eq(grid.cols(), 6, "grid resize updates columns");
        expect_eq(grid.rows(), 4, "grid resize updates rows");
        expect_eq(resized_cols, 6, "resize callback receives columns");
        expect_eq(resized_rows, 4, "resize callback receives rows");
    });

    run_test("ui event handler applies grapheme widths like nvim", []() {
        Grid grid;
        grid.resize(20, 1);

        HighlightTable highlights;
        UiEventHandler handler;
        handler.set_grid(&grid);
        handler.set_highlights(&highlights);

        const std::string combining = "e\xCC\x81";
        const std::string emoji = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
        const std::string flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
        const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
        const std::string keycap = "1\xEF\xB8\x8F\xE2\x83\xA3";
        const std::string heart = "\xE2\x9D\xA4";
        const std::string heart_emoji = "\xE2\x9D\xA4\xEF\xB8\x8F";
        const std::string cjk = "\xE7\x95\x8C";
        const std::string devanagari = "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7";
        const std::string lazy = "\xF3\xB0\x92\xB2";
        const std::string devicon = "\xEE\x98\xA0";

        handler.process_redraw({ redraw_event("grid_line", { grid_line_batch(1, 0, 0, {
                                                                                          cell(combining, 1),
                                                                                          cell(emoji, 2),
                                                                                          cell(flag, 3),
                                                                                          cell(family, 4),
                                                                                          cell(keycap, 5),
                                                                                          cell(heart, 6),
                                                                                          cell(heart_emoji, 7),
                                                                                          cell(cjk, 8),
                                                                                          cell(devanagari, 9),
                                                                                          cell(lazy, 10),
                                                                                          cell(devicon, 11),
                                                                                      }) }) });

        expect_eq(grid.get_cell(0, 0).text, combining, "combining cluster is preserved");
        expect_eq(grid.get_cell(0, 0).double_width, false, "combining cluster remains single width");
        expect_eq(grid.get_cell(1, 0).text, emoji, "emoji cluster is preserved");
        expect_eq(grid.get_cell(1, 0).double_width, true, "emoji cluster is treated as double width");
        expect(grid.get_cell(2, 0).double_width_cont, "emoji continuation cell is marked");
        expect_eq(grid.get_cell(3, 0).text, flag, "flag lands after the emoji cluster");
        expect_eq(grid.get_cell(3, 0).double_width, true, "flag is double width");
        expect(grid.get_cell(4, 0).double_width_cont, "flag continuation cell is marked");
        expect_eq(grid.get_cell(5, 0).text, family, "family zwj sequence lands after the flag");
        expect_eq(grid.get_cell(5, 0).double_width, true, "family zwj sequence is double width");
        expect(grid.get_cell(6, 0).double_width_cont, "family continuation cell is marked");
        expect_eq(grid.get_cell(7, 0).text, keycap, "keycap sequence remains single width");
        expect_eq(grid.get_cell(7, 0).double_width, false, "keycap sequence is single width");
        expect_eq(grid.get_cell(8, 0).text, heart, "text heart remains single width");
        expect_eq(grid.get_cell(8, 0).double_width, false, "text heart stays single width");
        expect_eq(grid.get_cell(9, 0).text, heart_emoji, "emoji heart is preserved");
        expect_eq(grid.get_cell(9, 0).double_width, true, "emoji heart becomes double width");
        expect(grid.get_cell(10, 0).double_width_cont, "emoji heart continuation cell is marked");
        expect_eq(grid.get_cell(11, 0).text, cjk, "cjk glyph lands after emoji heart");
        expect_eq(grid.get_cell(11, 0).double_width, true, "cjk glyph remains double width");
        expect(grid.get_cell(12, 0).double_width_cont, "cjk continuation cell is marked");
        expect_eq(grid.get_cell(13, 0).text, devanagari, "indic conjunct remains single width");
        expect_eq(grid.get_cell(13, 0).double_width, false, "indic conjunct stays single width");
        expect_eq(grid.get_cell(14, 0).text, lazy, "lazy icon remains single width");
        expect_eq(grid.get_cell(14, 0).double_width, false, "lazy icon stays single width");
        expect_eq(grid.get_cell(15, 0).text, devicon, "devicon remains single width");
        expect_eq(grid.get_cell(15, 0).double_width, false, "devicon stays single width");
    });
}
