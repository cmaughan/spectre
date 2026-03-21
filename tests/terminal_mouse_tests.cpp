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
// TestTerminalHost — exposes consume_output() and input helpers for tests.
// ---------------------------------------------------------------------------

class TestTerminalHost final : public TerminalHostBase
{
public:
    // Feed raw VT bytes into the parser.
    void feed(std::string_view bytes)
    {
        consume_output(bytes);
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

    // Helpers to synthesise mouse events.
    void press(int button, int px, int py, int mod = 0)
    {
        MouseButtonEvent ev;
        ev.button = button;
        ev.pressed = true;
        ev.x = px;
        ev.y = py;
        ev.mod = static_cast<uint16_t>(mod);
        host.on_mouse_button(ev);
    }

    void release(int button, int px, int py, int mod = 0)
    {
        MouseButtonEvent ev;
        ev.button = button;
        ev.pressed = false;
        ev.x = px;
        ev.y = py;
        ev.mod = static_cast<uint16_t>(mod);
        host.on_mouse_button(ev);
    }

    void move(int px, int py)
    {
        MouseMoveEvent ev;
        ev.x = px;
        ev.y = py;
        host.on_mouse_move(ev);
    }
};

// ---------------------------------------------------------------------------
// Helpers to decode mouse report bytes from the written buffer.
// ---------------------------------------------------------------------------

// Check whether 'written' contains an X10 (non-SGR) mouse report.
bool has_x10_report(const std::string& written)
{
    return written.size() >= 6 && written[0] == '\x1B' && written[1] == '[' && written[2] == 'M';
}

// Check whether 'written' contains an SGR mouse report starting with ESC[<.
bool has_sgr_report(const std::string& written)
{
    return written.size() >= 4 && written[0] == '\x1B' && written[1] == '[' && written[2] == '<';
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void run_terminal_mouse_tests()
{
    // -----------------------------------------------------------------------
    // No mouse mode: events must NOT produce any output
    // -----------------------------------------------------------------------

    run_test("mouse: no mode — press produces no output", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.press(1, 0, 0);
        expect(ts.host.written.empty(), "no output when mouse mode is None");
    });

    run_test("mouse: no mode — motion produces no output", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.press(1, 0, 0);
        ts.move(8, 16);
        // Only the Draxul selection path is active; that writes nothing.
        expect(ts.host.written.empty(), "no output when mouse mode is None");
    });

    run_test("mouse: no mode — release produces no output", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.press(1, 0, 0);
        ts.release(1, 0, 0);
        expect(ts.host.written.empty(), "no output when mouse mode is None");
    });

    // -----------------------------------------------------------------------
    // DECSET 1000 (button tracking): press and release emit events; motion does not
    // -----------------------------------------------------------------------

    run_test("mouse: DECSET 1000 — button press emits X10 report", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1000h"); // enable button tracking
        ts.press(1, 0, 0);
        expect(!ts.host.written.empty(), "press should emit a report");
        expect(has_x10_report(ts.host.written) || has_sgr_report(ts.host.written),
            "report should be X10 or SGR format");
    });

    run_test("mouse: DECSET 1000 — button release emits report", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1000h");
        ts.press(1, 0, 0);
        ts.host.written.clear();
        ts.release(1, 0, 0);
        expect(!ts.host.written.empty(), "release should emit a report");
    });

    run_test("mouse: DECSET 1000 — motion produces no report", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1000h");
        ts.press(1, 0, 0);
        ts.host.written.clear();
        ts.move(8, 0);
        // DECSET 1000 (Button mode) does not emit motion events.
        expect(ts.host.written.empty(), "motion must not emit in button mode");
    });

    // -----------------------------------------------------------------------
    // DECSET 1002 (drag/button-motion): motion with button held emits; without does not
    // -----------------------------------------------------------------------

    run_test("mouse: DECSET 1002 — press emits report", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1002h");
        ts.press(1, 0, 0);
        expect(!ts.host.written.empty(), "press emits in drag mode");
    });

    run_test("mouse: DECSET 1002 — motion without button held produces no output", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1002h");
        // Do not press any button; just move.
        ts.move(8, 0);
        expect(ts.host.written.empty(), "motion without button held must not emit in drag mode");
    });

    run_test("mouse: DECSET 1002 — motion with button held emits report", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1002h");
        ts.press(1, 0, 0);
        ts.host.written.clear();
        ts.move(8, 0); // move while button is held
        expect(!ts.host.written.empty(), "motion while button held must emit in drag mode");
    });

    run_test("mouse: DECSET 1002 — release then motion produces no output", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1002h");
        ts.press(1, 0, 0);
        ts.release(1, 0, 0);
        ts.host.written.clear();
        ts.move(8, 0);
        expect(ts.host.written.empty(), "motion after release must not emit in drag mode");
    });

    // -----------------------------------------------------------------------
    // DECSET 1003 (any-motion): motion always emits, with or without button
    // -----------------------------------------------------------------------

    run_test("mouse: DECSET 1003 — motion without button emits report", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1003h");
        ts.move(8, 0);
        expect(!ts.host.written.empty(), "motion without button must emit in any-motion mode");
    });

    run_test("mouse: DECSET 1003 — motion with button held emits report", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1003h");
        ts.press(1, 0, 0);
        ts.host.written.clear();
        ts.move(8, 0);
        expect(!ts.host.written.empty(), "motion with button held must emit in any-motion mode");
    });

    // -----------------------------------------------------------------------
    // SGR mouse (DECSET 1006) encoding for large coordinates
    // -----------------------------------------------------------------------

    run_test("mouse: SGR encoding used when DECSET 1006 is active", []() {
        TermSetup ts(80, 30);
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1000h\x1B[?1006h");
        // Press at a position that would exceed X10 byte encoding limits (col >= 223)
        // Cell 0 is at pixel 0; with cell width 8, col 240 = pixel 1920.
        ts.press(1, 1920, 0);
        expect(has_sgr_report(ts.host.written),
            "report should use SGR format for large coordinates");
    });

    run_test("mouse: SGR press and release use correct final chars", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1000h\x1B[?1006h");
        ts.press(1, 0, 0);
        // SGR press uses 'M' as final char.
        expect(ts.host.written.find('M') != std::string::npos, "SGR press uses 'M'");
        ts.host.written.clear();
        ts.release(1, 0, 0);
        // SGR release uses 'm' as final char.
        expect(ts.host.written.find('m') != std::string::npos, "SGR release uses 'm'");
    });

    // -----------------------------------------------------------------------
    // Disabling mouse mode restores no-output behaviour
    // -----------------------------------------------------------------------

    run_test("mouse: disabling DECSET 1003 reverts to no-output on motion", []() {
        TermSetup ts;
        expect(ts.ok, "host must initialize");
        ts.host.feed("\x1B[?1003h");
        ts.move(8, 0);
        expect(!ts.host.written.empty(), "motion emits while 1003 active");
        ts.host.feed("\x1B[?1003l"); // disable
        ts.host.written.clear();
        ts.move(16, 0);
        expect(ts.host.written.empty(), "motion must not emit after 1003 disabled");
    });
}
