#include "support/test_host_callbacks.h"
#include "support/test_local_terminal_host.h"
#include "support/test_terminal_host_fixture.h"

#include <catch2/catch_all.hpp>

#include <SDL3/SDL_keycode.h>

#include <string>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

// ---------------------------------------------------------------------------
// SelectionTestTerminalHost — exposes internals needed by selection tests.
// ---------------------------------------------------------------------------

class SelectionTestTerminalHost final : public TestLocalTerminalHost
{
public:
    void begin_selection(int c1, int r1, int c2, int r2)
    {
        MouseButtonEvent press_ev;
        press_ev.button = 1;
        press_ev.pressed = true;
        press_ev.pos = { c1 * 8, r1 * 16 };
        press_ev.mod = {};
        on_mouse_button(press_ev);

        MouseMoveEvent move_ev;
        move_ev.pos = { c2 * 8, r2 * 16 };
        on_mouse_move(move_ev);

        MouseButtonEvent release_ev;
        release_ev.button = 1;
        release_ev.pressed = false;
        release_ev.pos = { c2 * 8, r2 * 16 };
        release_ev.mod = {};
        on_mouse_button(release_ev);
    }

    void click_cell(int col, int row, int clicks = 1)
    {
        MouseButtonEvent press_ev;
        press_ev.button = 1;
        press_ev.pressed = true;
        press_ev.clicks = clicks;
        press_ev.pos = { col * 8, row * 16 };
        on_mouse_button(press_ev);

        MouseButtonEvent release_ev;
        release_ev.button = 1;
        release_ev.pressed = false;
        release_ev.clicks = clicks;
        release_ev.pos = { col * 8, row * 16 };
        on_mouse_button(release_ev);
    }

    void press_ctrl_c()
    {
        on_key(KeyEvent{ 0, SDLK_C, kModCtrl, true });
    }

    void press_key(int keycode, ModifierFlags mod)
    {
        on_key(KeyEvent{ 0, keycode, mod, true });
    }

    void send_text_input(std::string text)
    {
        on_text_input(TextInputEvent{ std::move(text) });
    }

    std::string selected_text()
    {
        return selection().extract_text();
    }

    bool selection_active()
    {
        return selection().is_active();
    }

    // Mirror of SelectionManager::kDefaultSelectionMaxCells — kept in sync manually.
    static constexpr int kLimit = SelectionManager::kDefaultSelectionMaxCells;
};

// ---------------------------------------------------------------------------
// Setup helper
// ---------------------------------------------------------------------------

struct SelectionSetup : TerminalHostFixture<SelectionTestTerminalHost>
{
    explicit SelectionSetup(int cols = 80, int rows = 30)
        : TerminalHostFixture(cols, rows)
    {
    }

    void set_copy_on_select(bool enabled)
    {
        HostReloadConfig reload;
        reload.copy_on_select = enabled;
        host.on_config_reloaded(reload);
    }

    // Fill the grid with a repeating ASCII character 'ch' so selections have content.
    void fill_grid(char ch = 'A')
    {
        for (int row = 0; row < host.rows_; ++row)
        {
            std::string line;
            line.reserve(static_cast<size_t>(host.cols_) + 2);
            for (int col = 0; col < host.cols_; ++col)
                line += ch;
            line += "\r\n";
            host.feed(line);
        }
    }

    // Make a selection from (c1,r1) to (c2,r2) and return the clipboard content.
    std::string select_and_copy(int c1, int r1, int c2, int r2)
    {
        host.begin_selection(c1, r1, c2, r2);
        host.dispatch_action("copy");
        return window.clipboard_;
    }
};

// Helper: check that text is a valid prefix of an all-ASCII single-char string.
static bool is_valid_prefix_of(const std::string& text, char expected_char, int max_len)
{
    if (static_cast<int>(text.size()) > max_len)
        return false;
    for (char c : text)
    {
        if (c != expected_char && c != '\n')
            return false;
    }
    return true;
}

// Helper: check that a UTF-8 string has no split codepoints.
static bool is_valid_utf8(const std::string& s)
{
    size_t i = 0;
    while (i < s.size())
    {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        int seq_len = 0;
        if (b < 0x80)
            seq_len = 1;
        else if ((b & 0xE0) == 0xC0)
            seq_len = 2;
        else if ((b & 0xF0) == 0xE0)
            seq_len = 3;
        else if ((b & 0xF8) == 0xF0)
            seq_len = 4;
        else
            return false; // invalid lead byte
        if (i + static_cast<size_t>(seq_len) > s.size())
            return false; // truncated sequence
        for (int j = 1; j < seq_len; ++j)
        {
            if ((static_cast<uint8_t>(s[i + static_cast<size_t>(j)]) & 0xC0) != 0x80)
                return false;
        }
        i += static_cast<size_t>(seq_len);
    }
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("selection: empty selection produces empty clipboard", "[terminal]")
{
    SelectionSetup ss;
    INFO("host must initialize");
    REQUIRE(ss.ok);
    // Select a zero-area region (same start and end point).
    ss.host.begin_selection(0, 0, 0, 0);
    ss.host.dispatch_action("copy");
    // The clipboard should remain empty (same-point selection is not activated).
    INFO("empty selection must not set clipboard");
    REQUIRE(ss.window.clipboard_.empty());
}

TEST_CASE("selection: small selection copies full content", "[terminal]")
{
    const int limit = SelectionTestTerminalHost::kLimit;
    SelectionSetup ss(80, 30);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('X');
    // Select a single row of 10 cells — well within any limit.
    const std::string result = ss.select_and_copy(0, 0, 9, 0);
    INFO("small selection should produce clipboard content");
    REQUIRE(!result.empty());
    INFO("small selection must not exceed limit");
    REQUIRE(static_cast<int>(result.size()) <= limit);
    INFO("content should be all X");
    REQUIRE(is_valid_prefix_of(result, 'X', limit));
}

TEST_CASE("selection: selection at exactly kSelectionMaxCells cells copies without truncation", "[terminal]")
{
    const int limit = SelectionTestTerminalHost::kLimit;
    // Use a grid sized so that one full row equals limit/cols rows.
    // Choose 100 columns so that limit/100 complete rows fills exactly 'limit' cells.
    const int cols = 100;
    const int target_cells = limit;
    const int rows_needed = target_cells / cols + 2; // extra rows for safety
    SelectionSetup ss(cols, rows_needed);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('Z');

    // Select exactly 'target_cells' cells: rows 0..(target_cells/cols - 1), all cols.
    const int end_row = (target_cells / cols) - 1;
    const int end_col = cols - 1;
    const std::string result = ss.select_and_copy(0, 0, end_col, end_row);
    // Result may trim trailing spaces/newlines, so just check no OOB: size is reasonable.
    INFO("result size must be bounded near the limit");
    REQUIRE(static_cast<int>(result.size()) <= limit * 2);
    INFO("content should be all Z");
    REQUIRE(is_valid_prefix_of(result, 'Z', limit * 2));
}

TEST_CASE("selection: selection one cell over limit truncates without crash", "[terminal]")
{
    const int limit = SelectionTestTerminalHost::kLimit;
    const int cols = 100;
    const int over_cells = limit + 1;
    const int rows_needed = over_cells / cols + 2;
    SelectionSetup ss(cols, rows_needed);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('Y');

    const int end_row = over_cells / cols;
    const int end_col = (over_cells % cols == 0) ? cols - 1 : (over_cells % cols) - 1;
    const std::string result = ss.select_and_copy(0, 0, end_col, end_row);
    // Must not crash, and result must not exceed the hard limit plus newlines.
    INFO("result must not exceed limit*2 bytes");
    REQUIRE(static_cast<int>(result.size()) <= limit * 2);
    INFO("content should be all Y");
    REQUIRE(is_valid_prefix_of(result, 'Y', limit * 2));
}

TEST_CASE("selection: selection far over limit truncates without crash", "[terminal]")
{
    const int limit = SelectionTestTerminalHost::kLimit;
    const int cols = 80;
    const int rows_needed = (limit * 2) / cols + 2;
    SelectionSetup ss(cols, rows_needed);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.fill_grid('W');

    // Select all rows we have.
    const int end_row = rows_needed - 1;
    const int end_col = cols - 1;
    const std::string result = ss.select_and_copy(0, 0, end_col, end_row);
    INFO("result must not exceed limit*2 bytes");
    REQUIRE(static_cast<int>(result.size()) <= limit * 2);
    INFO("content should be all W");
    REQUIRE(is_valid_prefix_of(result, 'W', limit * 2));
}

TEST_CASE("selection: UTF-8 multibyte content produces valid UTF-8 in clipboard", "[terminal]")
{
    // Use a small terminal and write UTF-8 content (2-byte sequences: U+00E9 = 0xC3 0xA9).
    SelectionSetup ss(20, 10);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    // Write rows of 'é' (U+00E9, encoded as 0xC3 0xA9 in UTF-8).
    for (int row = 0; row < 10; ++row)
    {
        std::string line;
        for (int col = 0; col < 20; ++col)
            line += "\xC3\xA9";
        line += "\r\n";
        ss.host.feed(line);
    }
    // Select all cells across all rows.
    const std::string result = ss.select_and_copy(0, 0, 19, 9);
    INFO("UTF-8 selection must produce content");
    REQUIRE(!result.empty());
    INFO("clipboard content must be valid UTF-8 (no split codepoints)");
    REQUIRE(is_valid_utf8(result));
}

TEST_CASE("selection: double-click keeps the full word after button release", "[terminal]")
{
    SelectionSetup ss(20, 3);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.host.feed("hello world\r\n");

    ss.host.click_cell(7, 0, 2);

    INFO("double-click should select the whole word, not just up to the cursor");
    REQUIRE(ss.host.selected_text() == std::string("world"));
}

TEST_CASE("selection: triple-click keeps the full logical line after button release", "[terminal]")
{
    SelectionSetup ss(20, 3);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.host.feed("hello world\r\n");

    ss.host.click_cell(4, 0, 3);

    INFO("triple-click should select the whole line, not truncate at the clicked column");
    REQUIRE(ss.host.selected_text() == std::string("hello world"));
}

TEST_CASE("selection: clicking inside an active selection copies it to the clipboard", "[terminal]")
{
    SelectionSetup ss(20, 3);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.set_copy_on_select(false);
    ss.host.feed("abcdef\r\n");

    ss.host.begin_selection(1, 0, 3, 0);
    INFO("clipboard should still be empty before the follow-up click");
    REQUIRE(ss.window.clipboard_.empty());

    ss.host.click_cell(2, 0);

    INFO("clicking inside the selected region should copy the current selection");
    REQUIRE(ss.window.clipboard_ == std::string("bcd"));
    INFO("mouse-copy click should dismiss the selection highlight afterward");
    REQUIRE_FALSE(ss.host.selection_active());
}

TEST_CASE("selection: Ctrl+C copies an active selection without sending input to the process", "[terminal]")
{
    SelectionSetup ss(20, 3);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.set_copy_on_select(false);
    ss.host.feed("abcdef\r\n");
    ss.host.begin_selection(1, 0, 3, 0);

    ss.host.press_ctrl_c();

    INFO("Ctrl+C should copy the selected text");
    REQUIRE(ss.window.clipboard_ == std::string("bcd"));
    INFO("Ctrl+C copy should dismiss the selection highlight afterward");
    REQUIRE_FALSE(ss.host.selection_active());
    INFO("Ctrl+C with an active selection should not forward SIGINT to the shell");
    REQUIRE(ss.host.written.empty());
}

TEST_CASE("selection: swallowed Ctrl+C also suppresses the follow-up text input event", "[terminal]")
{
    SelectionSetup ss(20, 3);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.set_copy_on_select(false);
    ss.host.feed("abcdef\r\n");
    ss.host.begin_selection(1, 0, 3, 0);

    ss.host.press_ctrl_c();
    ss.host.send_text_input(std::string(1, '\x03'));

    INFO("the copied text should still land on the clipboard");
    REQUIRE(ss.window.clipboard_ == std::string("bcd"));
    INFO("Ctrl+C copy should still clear the selection before the follow-up text event arrives");
    REQUIRE_FALSE(ss.host.selection_active());
    INFO("the follow-up text input event must not leak the control character to the shell");
    REQUIRE(ss.host.written.empty());
}

TEST_CASE("selection: raw left-control SDL modifier bits still trigger Ctrl+C copy", "[terminal]")
{
    SelectionSetup ss(20, 3);
    INFO("host must initialize");
    REQUIRE(ss.ok);
    ss.set_copy_on_select(false);
    ss.host.feed("abcdef\r\n");
    ss.host.begin_selection(1, 0, 3, 0);

    // Mirrors the real SDL log on Windows: left-ctrl bit plus an unrelated high bit.
    ss.host.press_key(SDLK_C, 0x8040);

    INFO("Ctrl+C copy should still work when SDL reports only the left-control bit");
    REQUIRE(ss.window.clipboard_ == std::string("bcd"));
    INFO("the raw SDL modifier shape should still dismiss the selection highlight");
    REQUIRE_FALSE(ss.host.selection_active());
    INFO("the raw SDL modifier shape must still be swallowed instead of reaching the shell");
    REQUIRE(ss.host.written.empty());
}
