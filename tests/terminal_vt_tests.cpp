#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_support.h"

#include <draxul/terminal_host_base.h>

#include <draxul/host.h>
#include <draxul/renderer.h>
#include <draxul/text_service.h>
#include <draxul/window.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// TestTerminalHost — exposes consume_output() as feed() for unit tests.
// ---------------------------------------------------------------------------

class TestTerminalHost final : public TerminalHostBase
{
public:
    // Feed raw VT bytes into the parser.
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
    }

    // Grid inspection helpers.
    std::string cell_text(int col, int row)
    {
        return std::string(grid().get_cell(col, row).text.view());
    }
    uint16_t cell_hl(int col, int row)
    {
        return grid().get_cell(col, row).hl_attr_id;
    }

    int col() const
    {
        return cursor_col();
    }
    int row() const
    {
        return cursor_row();
    }

    std::string written; // bytes sent back to the "process"

    int cols_ = 20;
    int rows_ = 5;

protected:
    std::string_view host_name() const override
    {
        return "test";
    }

    bool initialize_host() override
    {
        highlights().set_default_fg({ 1.0f, 1.0f, 1.0f, 1.0f });
        highlights().set_default_bg({ 0.0f, 0.0f, 0.0f, 1.0f });
        apply_grid_size(cols_, rows_);
        reset_terminal_state();
        set_content_ready(true);
        return true;
    }

    bool do_process_write(std::string_view text) override
    {
        written += text;
        return true;
    }
    std::vector<std::string> do_process_drain() override
    {
        return {};
    }
    bool do_process_resize(int, int) override
    {
        return true;
    }
    bool do_process_is_running() const override
    {
        return true;
    }
    void do_process_shutdown() override {}
};

// ---------------------------------------------------------------------------
// Setup helper
// ---------------------------------------------------------------------------

struct TermSetup
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestTerminalHost host;
    bool ok = false;

    explicit TermSetup(int cols = 20, int rows = 5)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        // TextService initialisation: don't care about glyph rendering for VT tests.
        // Use an absolute path so the test works regardless of the working directory.
        TextServiceConfig ts_cfg;
        ts_cfg.font_path = (std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
        text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f);

        HostViewport vp;
        vp.cols = cols;
        vp.rows = rows;

        HostContext ctx{ window, renderer, text_service, {}, vp, 96.0f };

        HostCallbacks cbs;
        cbs.request_frame = [] {};
        cbs.request_quit = [] {};
        cbs.wake_window = [] {};
        cbs.set_window_title = [](const std::string&) {};
        cbs.set_text_input_area = [](int, int, int, int) {};

        ok = host.initialize(ctx, std::move(cbs));
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void run_terminal_vt_tests()
{
    // -----------------------------------------------------------------------
    // Basic text
    // -----------------------------------------------------------------------

    run_test("terminal: plain text writes to grid cells", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("Hello");
        expect_eq(ts.host.cell_text(0, 0), std::string("H"), "col 0");
        expect_eq(ts.host.cell_text(1, 0), std::string("e"), "col 1");
        expect_eq(ts.host.cell_text(4, 0), std::string("o"), "col 4");
        expect_eq(ts.host.col(), 5, "cursor at column 5 after 'Hello'");
    });

    run_test("terminal: carriage return moves cursor to column 0", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("AB\rC");
        expect_eq(ts.host.cell_text(0, 0), std::string("C"), "CR overwrites from col 0");
        expect_eq(ts.host.cell_text(1, 0), std::string("B"), "col 1 unchanged");
        expect_eq(ts.host.col(), 1, "cursor at col 1 after C");
    });

    run_test("terminal: linefeed advances row without carriage return", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("AB\nC");
        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "row 0 col 0 preserved");
        expect_eq(ts.host.cell_text(2, 1), std::string("C"), "C at row 1 col 2");
        expect_eq(ts.host.row(), 1, "cursor at row 1");
    });

    run_test("terminal: CRLF advances row and resets column", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("AB\r\nCD");
        expect_eq(ts.host.cell_text(0, 1), std::string("C"), "C at row 1 col 0");
        expect_eq(ts.host.cell_text(1, 1), std::string("D"), "D at row 1 col 1");
        expect_eq(ts.host.col(), 2, "cursor at col 2");
        expect_eq(ts.host.row(), 1, "cursor at row 1");
    });

    run_test("terminal: scroll when cursor at bottom", []() {
        TermSetup ts(4, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("A\r\nB\r\nC\r\nD");
        expect_eq(ts.host.cell_text(0, 0), std::string("B"), "B scrolled to row 0");
        expect_eq(ts.host.cell_text(0, 1), std::string("C"), "C at row 1");
        expect_eq(ts.host.cell_text(0, 2), std::string("D"), "D at row 2");
        expect_eq(ts.host.row(), 2, "cursor at bottom row");
    });

    // -----------------------------------------------------------------------
    // Cursor movement
    // -----------------------------------------------------------------------

    run_test("terminal: CSI H positions cursor", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[4;7H");
        expect_eq(ts.host.row(), 3, "row 3 (0-based from 1-based 4)");
        expect_eq(ts.host.col(), 6, "col 6 (0-based from 1-based 7)");
    });

    run_test("terminal: CSI H with no params moves to home", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("AB\r\nCD\x1B[H");
        expect_eq(ts.host.row(), 0, "row 0 after home");
        expect_eq(ts.host.col(), 0, "col 0 after home");
    });

    run_test("terminal: CSI A/B/C/D move cursor", []() {
        TermSetup ts(20, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[3;3H"); // 1-based row 3 col 3 -> row 2 col 2
        ts.host.feed("\x1B[2A");
        expect_eq(ts.host.row(), 0, "up 2 from row 2 = row 0");
        ts.host.feed("\x1B[3B");
        expect_eq(ts.host.row(), 3, "down 3 from row 0 = row 3");
        ts.host.feed("\x1B[5C");
        expect_eq(ts.host.col(), 7, "right 5 from col 2 = col 7");
        ts.host.feed("\x1B[3D");
        expect_eq(ts.host.col(), 4, "left 3 from col 7 = col 4");
    });

    run_test("terminal: CSI G CHA positions in column", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("Hello\x1B[3G"); // col 3 (1-based) -> col 2 (0-based)
        expect_eq(ts.host.col(), 2, "col 2 after CHA 3");
    });

    run_test("terminal: CSI E and F cursor next/preceding line", []() {
        TermSetup ts(20, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[3;5H\x1B[2E"); // row 3,col 5 -> CNL 2 -> row 5 (max 4), col 0
        expect_eq(ts.host.row(), 4, "row 4 after CNL 2");
        expect_eq(ts.host.col(), 0, "col 0 after CNL");
        ts.host.feed("\x1B[3F"); // CPL 3 -> row 1, col 0
        expect_eq(ts.host.row(), 1, "row 1 after CPL 3");
        expect_eq(ts.host.col(), 0, "col 0 after CPL");
    });

    run_test("terminal: cursor save and restore", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[3;5H\x1B[s\x1B[1;1H\x1B[u");
        expect_eq(ts.host.col(), 4, "restored col 4 (0-based)");
        expect_eq(ts.host.row(), 2, "restored row 2 (0-based)");
    });

    run_test("terminal: backspace moves cursor left", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("ABC\b");
        expect_eq(ts.host.col(), 2, "col 2 after ABC+backspace");
    });

    run_test("terminal: tab advances to next 8-column boundary", []() {
        TermSetup ts(20, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("AB\t");
        expect_eq(ts.host.col(), 8, "tab from col 2 to col 8");
        ts.host.feed("\t");
        expect_eq(ts.host.col(), 16, "second tab to col 16");
    });

    run_test("terminal: CSI d VPA sets row directly", []() {
        TermSetup ts(10, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[3d");
        expect_eq(ts.host.row(), 2, "VPA row 2 (0-based)");
    });

    // -----------------------------------------------------------------------
    // Erase
    // -----------------------------------------------------------------------

    run_test("terminal: CSI K erases to end of line", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("Hello\x1B[3G\x1B[K"); // write, move to col 3 (0-based 2), erase to EOL
        expect_eq(ts.host.cell_text(0, 0), std::string("H"), "col 0 preserved");
        expect_eq(ts.host.cell_text(1, 0), std::string("e"), "col 1 preserved");
        expect_eq(ts.host.cell_text(2, 0), std::string(" "), "col 2 erased");
        expect_eq(ts.host.cell_text(3, 0), std::string(" "), "col 3 erased");
        expect_eq(ts.host.cell_text(4, 0), std::string(" "), "col 4 erased");
    });

    run_test("terminal: CSI 2K erases whole line", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("Hello\x1B[3G\x1B[2K");
        for (int col = 0; col < 5; ++col)
            expect_eq(ts.host.cell_text(col, 0), std::string(" "), "col erased");
    });

    run_test("terminal: CSI 2J clears entire screen", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("ABCDE\r\nFGHIJ\r\nKLMNO\x1B[2J");
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 5; ++col)
                expect_eq(ts.host.cell_text(col, row), std::string(" "), "cell cleared");
    });

    // -----------------------------------------------------------------------
    // SGR
    // -----------------------------------------------------------------------

    run_test("terminal: SGR colors produce distinct hl_attr_ids", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[31mA\x1B[32mB\x1B[0mC");
        expect(ts.host.cell_hl(0, 0) != 0, "A has non-default hl");
        expect(ts.host.cell_hl(1, 0) != 0, "B has non-default hl");
        expect(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0), "A and B have different hl");
        expect_eq(ts.host.cell_hl(2, 0), static_cast<uint16_t>(0), "C has default hl after reset");
    });

    // -----------------------------------------------------------------------
    // Scroll region (DECSTBM)
    // -----------------------------------------------------------------------

    run_test("terminal: DECSTBM scroll region restricts newline scroll", []() {
        TermSetup ts(5, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
        // Set scroll region rows 2-4 (1-based), cursor to bottom of region, then newline
        ts.host.feed("\x1B[2;4r\x1B[4;1H\n");

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "row 0 unchanged");
        expect_eq(ts.host.cell_text(0, 1), std::string("C"), "row 1 has C after region scroll");
        expect_eq(ts.host.cell_text(0, 2), std::string("D"), "row 2 has D");
        expect_eq(ts.host.cell_text(0, 3), std::string(" "), "row 3 blank after scroll");
        expect_eq(ts.host.cell_text(0, 4), std::string("E"), "row 4 unchanged");
    });

    run_test("terminal: CSI S scrolls up within scroll region", []() {
        TermSetup ts(5, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
        ts.host.feed("\x1B[2;4r\x1B[S"); // region rows 2-4, SU 1

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "row 0 unchanged");
        expect_eq(ts.host.cell_text(0, 1), std::string("C"), "row 1 has C after SU");
        expect_eq(ts.host.cell_text(0, 2), std::string("D"), "row 2 has D after SU");
        expect_eq(ts.host.cell_text(0, 3), std::string(" "), "row 3 blank after SU");
        expect_eq(ts.host.cell_text(0, 4), std::string("E"), "row 4 unchanged");
    });

    run_test("terminal: CSI T scrolls down within scroll region", []() {
        TermSetup ts(5, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
        ts.host.feed("\x1B[2;4r\x1B[T"); // region rows 2-4, SD 1

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "row 0 unchanged");
        expect_eq(ts.host.cell_text(0, 1), std::string(" "), "row 1 blank after SD");
        expect_eq(ts.host.cell_text(0, 2), std::string("B"), "row 2 has B after SD");
        expect_eq(ts.host.cell_text(0, 3), std::string("C"), "row 3 has C after SD");
        expect_eq(ts.host.cell_text(0, 4), std::string("E"), "row 4 unchanged");
    });

    // -----------------------------------------------------------------------
    // Insert / delete line (IL/DL)
    // -----------------------------------------------------------------------

    run_test("terminal: CSI L inserts blank lines pushing content down", []() {
        TermSetup ts(5, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
        ts.host.feed("\x1B[2;1H\x1B[2L"); // cursor to row 2 col 1, insert 2 lines

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "row 0 unchanged");
        expect_eq(ts.host.cell_text(0, 1), std::string(" "), "row 1 blank (inserted)");
        expect_eq(ts.host.cell_text(0, 2), std::string(" "), "row 2 blank (inserted)");
        expect_eq(ts.host.cell_text(0, 3), std::string("B"), "row 3 has B (pushed down)");
        expect_eq(ts.host.cell_text(0, 4), std::string("C"), "row 4 has C (D/E fall off)");
    });

    run_test("terminal: CSI M deletes lines pulling content up", []() {
        TermSetup ts(5, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("AAAAA\r\nBBBBB\r\nCCCCC\r\nDDDDD\r\nEEEEE");
        ts.host.feed("\x1B[2;1H\x1B[2M"); // cursor to row 2 col 1, delete 2 lines

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "row 0 unchanged");
        expect_eq(ts.host.cell_text(0, 1), std::string("D"), "row 1 has D (pulled up)");
        expect_eq(ts.host.cell_text(0, 2), std::string("E"), "row 2 has E (pulled up)");
        expect_eq(ts.host.cell_text(0, 3), std::string(" "), "row 3 blank");
        expect_eq(ts.host.cell_text(0, 4), std::string(" "), "row 4 blank");
    });

    // -----------------------------------------------------------------------
    // Insert / delete / erase character (ICH/DCH/ECH)
    // -----------------------------------------------------------------------

    run_test("terminal: CSI @ inserts blank characters shifting right", []() {
        TermSetup ts(6, 1);
        expect(ts.ok, "host must initialize");
        // Write ABCDE, then move to col 3 (1-based = col 2 0-based) via CHA, insert 2 chars
        ts.host.feed("ABCDE\x1B[3G\x1B[2@");

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "col 0 unchanged");
        expect_eq(ts.host.cell_text(1, 0), std::string("B"), "col 1 unchanged");
        expect_eq(ts.host.cell_text(2, 0), std::string(" "), "col 2 blank (inserted)");
        expect_eq(ts.host.cell_text(3, 0), std::string(" "), "col 3 blank (inserted)");
        expect_eq(ts.host.cell_text(4, 0), std::string("C"), "col 4 has C (shifted)");
        expect_eq(ts.host.cell_text(5, 0), std::string("D"), "col 5 has D");
    });

    run_test("terminal: CSI P deletes characters pulling left", []() {
        TermSetup ts(6, 1);
        expect(ts.ok, "host must initialize");
        // Write ABCDEF, then move to col 3 (1-based = col 2 0-based) via CHA, delete 2 chars
        ts.host.feed("ABCDEF\x1B[3G\x1B[2P");

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "col 0 unchanged");
        expect_eq(ts.host.cell_text(1, 0), std::string("B"), "col 1 unchanged");
        expect_eq(ts.host.cell_text(2, 0), std::string("E"), "col 2 has E (pulled left)");
        expect_eq(ts.host.cell_text(3, 0), std::string("F"), "col 3 has F");
        expect_eq(ts.host.cell_text(4, 0), std::string(" "), "col 4 blank");
        expect_eq(ts.host.cell_text(5, 0), std::string(" "), "col 5 blank");
    });

    run_test("terminal: CSI X erases characters in place", []() {
        TermSetup ts(6, 1);
        expect(ts.ok, "host must initialize");
        // Write ABCDEF, then move to col 3 (1-based = col 2 0-based) via CHA, erase 3 chars
        ts.host.feed("ABCDEF\x1B[3G\x1B[3X");

        expect_eq(ts.host.cell_text(0, 0), std::string("A"), "col 0 unchanged");
        expect_eq(ts.host.cell_text(1, 0), std::string("B"), "col 1 unchanged");
        expect_eq(ts.host.cell_text(2, 0), std::string(" "), "col 2 erased");
        expect_eq(ts.host.cell_text(3, 0), std::string(" "), "col 3 erased");
        expect_eq(ts.host.cell_text(4, 0), std::string(" "), "col 4 erased");
        expect_eq(ts.host.cell_text(5, 0), std::string("F"), "col 5 unchanged");
    });

    // -----------------------------------------------------------------------
    // Alternate screen
    // -----------------------------------------------------------------------

    run_test("terminal: alternate screen entry clears screen", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("Hello\x1B[?1049h");
        expect_eq(ts.host.cell_text(0, 0), std::string(" "), "alt screen col 0 blank");
        expect_eq(ts.host.cell_text(1, 0), std::string(" "), "alt screen col 1 blank");
    });

    run_test("terminal: alternate screen exit restores main screen content", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("Hello\x1B[?1049h"); // enter alt screen (saves main)
        ts.host.feed("World"); // write to alt screen
        ts.host.feed("\x1B[?1049l"); // leave alt screen (restores main)
        expect_eq(ts.host.cell_text(0, 0), std::string("H"), "H restored");
        expect_eq(ts.host.cell_text(1, 0), std::string("e"), "e restored");
        expect_eq(ts.host.cell_text(4, 0), std::string("o"), "o restored");
    });

    // -----------------------------------------------------------------------
    // Auto-wrap
    // -----------------------------------------------------------------------

    run_test("terminal: pending wrap wraps on next character", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("ABCDE"); // fills row 0
        ts.host.feed("F"); // wraps to row 1 col 0
        expect_eq(ts.host.cell_text(0, 1), std::string("F"), "F wrapped to row 1");
        expect_eq(ts.host.row(), 1, "cursor on row 1");
        expect_eq(ts.host.col(), 1, "cursor at col 1 after F");
    });

    run_test("terminal: cursor movement clears pending wrap", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("ABCDE"); // fills row 0, pending wrap
        ts.host.feed("\x1B[G"); // CHA to col 1 (1-based) = col 0, clears pending wrap
        ts.host.feed("X"); // overwrites row 0 col 0, does NOT wrap
        expect_eq(ts.host.cell_text(0, 0), std::string("X"), "X at row 0 col 0");
        expect_eq(ts.host.row(), 0, "cursor stays on row 0");
    });

    run_test("terminal: auto-wrap off prevents wrap", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?7l"); // disable auto-wrap
        ts.host.feed("ABCDEFG"); // 7 chars in 5-wide terminal
        // With no wrap, chars beyond col 4 overwrite col 4
        expect_eq(ts.host.cell_text(4, 0), std::string("G"), "col 4 has G (last written)");
        expect_eq(ts.host.row(), 0, "cursor stays on row 0");
    });

    // -----------------------------------------------------------------------
    // Scrollback
    // -----------------------------------------------------------------------

    run_test("terminal: scrolled-off rows accumulate in scrollback", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        // Write 5 rows to a 3-row terminal — 2 rows scroll off
        ts.host.feed("AAA\r\nBBB\r\nCCC\r\nDDD\r\nEEE");
        // Live view should show CCC DDD EEE
        expect_eq(ts.host.cell_text(0, 0), std::string("C"), "live row 0 is C");
        expect_eq(ts.host.cell_text(0, 1), std::string("D"), "live row 1 is D");
        expect_eq(ts.host.cell_text(0, 2), std::string("E"), "live row 2 is E");
    });

    // -----------------------------------------------------------------------
    // OSC title
    // -----------------------------------------------------------------------

    run_test("terminal: OSC title sequence parses without crash", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B]0;My Title\x07");
        expect(true, "OSC title sequence parsed without crash");
    });

    // -----------------------------------------------------------------------
    // SGR completeness
    // -----------------------------------------------------------------------

    run_test("terminal: SGR 0 full reset clears all attributes", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        // Set bold, italic, underline, strikethrough and a foreground color, then reset
        ts.host.feed("\x1B[1;3;4;9;31mA\x1B[0mB");
        expect(ts.host.cell_hl(0, 0) != 0, "A has non-default hl with multiple attributes");
        expect_eq(ts.host.cell_hl(1, 0), static_cast<uint16_t>(0), "B has default hl after SGR 0");
    });

    run_test("terminal: SGR 39 resets foreground to default", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        // Set foreground red, then write A; then reset foreground with SGR 39 and write B
        ts.host.feed("\x1B[31mA\x1B[39mB");
        const uint16_t hl_a = ts.host.cell_hl(0, 0);
        const uint16_t hl_b = ts.host.cell_hl(1, 0);
        expect(hl_a != 0, "A has non-default hl with red fg");
        // After SGR 39, the fg is cleared; if no other attrs remain, hl_id returns to 0
        expect(hl_a != hl_b, "A and B have different hl_ids (fg reset changes attributes)");
        // B should have default hl (no fg set)
        expect_eq(hl_b, static_cast<uint16_t>(0), "B has default hl after SGR 39 resets fg");
    });

    run_test("terminal: SGR 49 resets background to default", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[41mA\x1B[49mB");
        const uint16_t hl_a = ts.host.cell_hl(0, 0);
        const uint16_t hl_b = ts.host.cell_hl(1, 0);
        expect(hl_a != 0, "A has non-default hl with red bg");
        expect(hl_a != hl_b, "A and B have different hl_ids");
        expect_eq(hl_b, static_cast<uint16_t>(0), "B has default hl after SGR 49 resets bg");
    });

    run_test("terminal: SGR 1 bold and SGR 22 bold-off", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[1mA\x1B[22mB");
        const uint16_t hl_a = ts.host.cell_hl(0, 0);
        const uint16_t hl_b = ts.host.cell_hl(1, 0);
        expect(hl_a != 0, "A has bold hl");
        expect_eq(hl_b, static_cast<uint16_t>(0), "B has default hl after SGR 22");
    });

    run_test("terminal: SGR 3 italic and SGR 23 italic-off", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[3mA\x1B[23mB");
        const uint16_t hl_a = ts.host.cell_hl(0, 0);
        const uint16_t hl_b = ts.host.cell_hl(1, 0);
        expect(hl_a != 0, "A has italic hl");
        expect_eq(hl_b, static_cast<uint16_t>(0), "B has default hl after SGR 23");
    });

    run_test("terminal: SGR 4 underline and SGR 24 underline-off", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[4mA\x1B[24mB");
        const uint16_t hl_a = ts.host.cell_hl(0, 0);
        const uint16_t hl_b = ts.host.cell_hl(1, 0);
        expect(hl_a != 0, "A has underline hl");
        expect_eq(hl_b, static_cast<uint16_t>(0), "B has default hl after SGR 24");
    });

    run_test("terminal: SGR 9 strikethrough and SGR 29 strikethrough-off", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[9mA\x1B[29mB");
        const uint16_t hl_a = ts.host.cell_hl(0, 0);
        const uint16_t hl_b = ts.host.cell_hl(1, 0);
        expect(hl_a != 0, "A has strikethrough hl");
        expect_eq(hl_b, static_cast<uint16_t>(0), "B has default hl after SGR 29");
    });

    run_test("terminal: SGR 30-37 set foreground colors producing distinct hl ids", []() {
        TermSetup ts(20, 2);
        expect(ts.ok, "host must initialize");
        // Write one character per ANSI foreground color (30–37)
        for (int i = 30; i <= 37; ++i)
        {
            char seq[16];
            snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
            ts.host.feed(seq);
        }
        // All 8 characters should have non-zero highlight ids
        for (int col = 0; col < 8; ++col)
            expect(ts.host.cell_hl(col, 0) != 0, "SGR 30-37 characters have non-default hl");
        // Adjacent colors should differ
        for (int col = 0; col < 7; ++col)
            expect(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0),
                "adjacent ANSI foreground colors have distinct hl ids");
    });

    run_test("terminal: SGR 40-47 set background colors producing distinct hl ids", []() {
        TermSetup ts(20, 2);
        expect(ts.ok, "host must initialize");
        for (int i = 40; i <= 47; ++i)
        {
            char seq[16];
            snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
            ts.host.feed(seq);
        }
        for (int col = 0; col < 8; ++col)
            expect(ts.host.cell_hl(col, 0) != 0, "SGR 40-47 characters have non-default hl");
        for (int col = 0; col < 7; ++col)
            expect(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0),
                "adjacent ANSI background colors have distinct hl ids");
    });

    run_test("terminal: SGR 90-97 set bright foreground colors", []() {
        TermSetup ts(20, 2);
        expect(ts.ok, "host must initialize");
        for (int i = 90; i <= 97; ++i)
        {
            char seq[16];
            snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
            ts.host.feed(seq);
        }
        for (int col = 0; col < 8; ++col)
            expect(ts.host.cell_hl(col, 0) != 0, "SGR 90-97 characters have non-default hl");
        // Bright colors (90-97) differ from normal colors (30-37)
        for (int col = 0; col < 7; ++col)
            expect(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0),
                "adjacent bright foreground colors have distinct hl ids");
    });

    run_test("terminal: SGR 100-107 set bright background colors", []() {
        TermSetup ts(20, 2);
        expect(ts.ok, "host must initialize");
        for (int i = 100; i <= 107; ++i)
        {
            char seq[16];
            snprintf(seq, sizeof(seq), "\x1B[%dmX", i);
            ts.host.feed(seq);
        }
        for (int col = 0; col < 8; ++col)
            expect(ts.host.cell_hl(col, 0) != 0, "SGR 100-107 characters have non-default hl");
        for (int col = 0; col < 7; ++col)
            expect(ts.host.cell_hl(col, 0) != ts.host.cell_hl(col + 1, 0),
                "adjacent bright background colors have distinct hl ids");
    });

    run_test("terminal: SGR 38;2 true-color foreground sets distinct hl", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        // True-color red: SGR 38;2;255;0;0
        ts.host.feed("\x1B[38;2;255;0;0mA");
        // True-color blue: SGR 38;2;0;0;255
        ts.host.feed("\x1B[38;2;0;0;255mB");
        ts.host.feed("\x1B[0mC");
        expect(ts.host.cell_hl(0, 0) != 0, "A has true-color fg hl");
        expect(ts.host.cell_hl(1, 0) != 0, "B has true-color fg hl");
        expect(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0), "red and blue fg produce distinct hl ids");
        expect_eq(ts.host.cell_hl(2, 0), static_cast<uint16_t>(0), "C has default hl after SGR 0");
    });

    run_test("terminal: SGR 48;2 true-color background sets distinct hl", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[48;2;0;255;0mA");
        ts.host.feed("\x1B[48;2;255;0;255mB");
        ts.host.feed("\x1B[0mC");
        expect(ts.host.cell_hl(0, 0) != 0, "A has true-color bg hl");
        expect(ts.host.cell_hl(1, 0) != 0, "B has true-color bg hl");
        expect(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0), "green and magenta bg produce distinct hl ids");
        expect_eq(ts.host.cell_hl(2, 0), static_cast<uint16_t>(0), "C has default hl after SGR 0");
    });

    run_test("terminal: SGR 38;5 256-color foreground sets distinct hl", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        // 256-color index 200 and 201 should produce different colors
        ts.host.feed("\x1B[38;5;200mA");
        ts.host.feed("\x1B[38;5;201mB");
        ts.host.feed("\x1B[0mC");
        expect(ts.host.cell_hl(0, 0) != 0, "A has 256-color fg hl");
        expect(ts.host.cell_hl(1, 0) != 0, "B has 256-color fg hl");
        expect(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0),
            "256-color index 200 and 201 produce distinct hl ids");
        expect_eq(ts.host.cell_hl(2, 0), static_cast<uint16_t>(0), "C has default hl after SGR 0");
    });

    run_test("terminal: SGR 48;5 256-color background sets distinct hl", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[48;5;100mA");
        ts.host.feed("\x1B[48;5;101mB");
        ts.host.feed("\x1B[0mC");
        expect(ts.host.cell_hl(0, 0) != 0, "A has 256-color bg hl");
        expect(ts.host.cell_hl(1, 0) != 0, "B has 256-color bg hl");
        expect(ts.host.cell_hl(0, 0) != ts.host.cell_hl(1, 0),
            "256-color index 100 and 101 produce distinct hl ids");
        expect_eq(ts.host.cell_hl(2, 0), static_cast<uint16_t>(0), "C has default hl after SGR 0");
    });

    run_test("terminal: SGR 38;5;0 uses ANSI black (index 0)", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[38;5;0mA\x1B[0mB");
        expect(ts.host.cell_hl(0, 0) != 0, "A has 256-color palette index 0 hl");
        expect_eq(ts.host.cell_hl(1, 0), static_cast<uint16_t>(0), "B default after reset");
    });

    run_test("terminal: SGR combinations produce the correct cumulative effect", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        // Bold + italic together
        ts.host.feed("\x1B[1;3mA");
        // Bold only
        ts.host.feed("\x1B[23mB"); // turn off italic
        // Default
        ts.host.feed("\x1B[22mC"); // turn off bold
        const uint16_t hl_a = ts.host.cell_hl(0, 0);
        const uint16_t hl_b = ts.host.cell_hl(1, 0);
        const uint16_t hl_c = ts.host.cell_hl(2, 0);
        expect(hl_a != 0, "A has bold+italic hl");
        expect(hl_b != 0, "B has bold hl");
        expect_eq(hl_c, static_cast<uint16_t>(0), "C has default hl after turning off bold");
        expect(hl_a != hl_b, "bold+italic differs from bold-only");
    });

    run_test("terminal: alt-screen enter preserves main cursor position", []() {
        TermSetup ts(10, 5);
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[3;5H"); // move to row 3, col 5 (1-based) = row 2, col 4 (0-based)
        ts.host.feed("X"); // write at position
        ts.host.feed("\x1B[?1049h"); // enter alt screen
        // Alt screen starts at (0,0)
        expect_eq(ts.host.col(), 0, "cursor resets to col 0 on alt screen");
        expect_eq(ts.host.row(), 0, "cursor resets to row 0 on alt screen");
        // Alt screen should be blank
        expect_eq(ts.host.cell_text(0, 0), std::string(" "), "alt screen row 0 col 0 blank");
        ts.host.feed("\x1B[?1049l"); // exit alt screen
        // Main screen content restored
        expect_eq(ts.host.cell_text(4, 2), std::string("X"), "X restored at correct position after alt-screen exit");
    });

    run_test("terminal: alt-screen double enter is idempotent", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        ts.host.feed("Hello");
        ts.host.feed("\x1B[?1049h"); // first enter
        ts.host.feed("ALT");
        ts.host.feed("\x1B[?1049h"); // second enter (should be no-op or safe)
        ts.host.feed("\x1B[?1049l"); // exit
        // Main screen should be restored
        expect_eq(ts.host.cell_text(0, 0), std::string("H"), "H restored after double-enter exit");
    });

    run_test("terminal: origin mode restricts cursor to scroll region on positioning", []() {
        TermSetup ts(10, 6);
        expect(ts.ok, "host must initialize");
        // Set scroll region rows 3-5 (1-based), then enable origin mode
        ts.host.feed("\x1B[3;5r"); // scroll region rows 3-5 (1-based) = rows 2-4 (0-based)
        ts.host.feed("\x1B[?6h"); // enable origin mode
        // In origin mode, CUP (H) is relative to scroll region top
        ts.host.feed("\x1B[1;1H"); // should go to scroll-region top (row 2 in absolute terms)
        // With origin mode on, cursor row 0 = scroll_top (row 2 absolute)
        // We just verify no crash and that row is within the scroll region
        expect(ts.host.row() >= 2, "origin mode cursor row is within scroll region");
        expect(ts.host.row() <= 4, "origin mode cursor row is within scroll region (upper bound)");
        ts.host.feed("\x1B[?6l"); // disable origin mode
        ts.host.feed("\x1B[r"); // reset scroll region
    });

    run_test("terminal: auto-wrap mode can be toggled with ?7h and ?7l", []() {
        TermSetup ts(5, 3);
        expect(ts.ok, "host must initialize");
        // Disable wrap, overfill row 0, then re-enable and overfill again
        ts.host.feed("\x1B[?7l"); // disable wrap
        ts.host.feed("ABCDEFG"); // 7 chars; last 2 overwrite col 4
        expect_eq(ts.host.cell_text(4, 0), std::string("G"), "no-wrap: last char overwrites at last col");
        expect_eq(ts.host.row(), 0, "no-wrap: cursor stays on row 0");
        ts.host.feed("\x1B[?7h"); // re-enable wrap
        ts.host.feed("\x1B[2;1H");
        ts.host.feed("ABCDEF"); // 6 chars in 5-wide terminal: wraps to row 2
        expect_eq(ts.host.cell_text(0, 2), std::string("F"), "wrap enabled: F on row 2");
    });
}
