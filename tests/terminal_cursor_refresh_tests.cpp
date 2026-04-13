#include <catch2/catch_all.hpp>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <draxul/base64.h>
#include <draxul/terminal_host_base.h>
#include <draxul/text_service.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string bundled_font_path()
{
    return (std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf").string();
}

class SplitRefreshHost final : public TerminalHostBase
{
public:
    void feed_immediate(std::string_view bytes)
    {
        consume_output(bytes);
    }

    void queue_drain(std::vector<std::string> chunks)
    {
        queued_drains_.push_back(std::move(chunks));
    }

    int vt_col() const
    {
        return vt_state().col;
    }

    int vt_row() const
    {
        return vt_state().row;
    }

    bool vt_cursor_visible() const
    {
        return vt_state().cursor_visible;
    }

    int cols_ = 20;
    int rows_ = 10;

protected:
    bool initialize_host() override
    {
        highlights().set_default_fg({ 1.0f, 1.0f, 1.0f, 1.0f });
        highlights().set_default_bg({ 0.0f, 0.0f, 0.0f, 1.0f });
        apply_grid_size(cols_, rows_);
        reset_terminal_state();
        update_cursor_style();
        set_content_ready(true);
        return true;
    }

    std::string_view host_name() const override
    {
        return "split-refresh-test";
    }

    bool do_process_write(std::string_view) override
    {
        return true;
    }

    std::vector<std::string> do_process_drain() override
    {
        if (queued_drains_.empty())
            return {};
        auto chunks = std::move(queued_drains_.front());
        queued_drains_.pop_front();
        return chunks;
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

private:
    std::deque<std::vector<std::string>> queued_drains_;
};

struct TerminalCursorRefreshHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    SplitRefreshHost host;
    TestHostCallbacks callbacks;
    bool ok = false;

    explicit TerminalCursorRefreshHarness(int cols = 20, int rows = 10)
    {
        host.cols_ = cols;
        host.rows_ = rows;

        TextServiceConfig ts_cfg;
        ts_cfg.font_path = bundled_font_path();
        REQUIRE(text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f));

        HostViewport viewport;
        viewport.pixel_size = { 800, 600 };
        viewport.grid_size = { host.cols_, host.rows_ };

        HostContext context{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .initial_viewport = viewport,
            .display_ppi = 96.0f,
        };

        ok = host.initialize(context, callbacks);
        REQUIRE(renderer.last_handle != nullptr);
    }
};

std::vector<std::string> decode_capture_chunks(std::initializer_list<std::string_view> encoded_chunks)
{
    std::vector<std::string> decoded;
    decoded.reserve(encoded_chunks.size());
    for (std::string_view encoded : encoded_chunks)
    {
        auto bytes = base64_decode(encoded);
        REQUIRE(bytes.has_value());
        decoded.push_back(std::move(*bytes));
    }
    return decoded;
}

std::string cursor_points_to_string(const std::vector<glm::ivec2>& points)
{
    std::ostringstream out;
    bool first = true;
    for (const auto& point : points)
    {
        if (!first)
            out << " -> ";
        first = false;
        out << "(" << point.x << "," << point.y << ")";
    }
    return out.str();
}

} // namespace

TEST_CASE("terminal cursor stays pinned across split status redraw bursts", "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    INFO("baseline prompt cursor is visible before the split redraw");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    h.host.queue_drain({ "\x1B"
                         "7\x1B[10;1HSTATUS" });
    h.host.pump();
    INFO("intermediate status-line chunk keeps the cursor pinned at the saved prompt position");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);

    h.host.queue_drain({ "\x1B"
                         "8" });
    h.host.pump();
    INFO("final restore chunk keeps the cursor steady instead of flashing away and back");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
}

TEST_CASE("terminal cursor stays pinned when save-move-restore repaint arrives in one burst", "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    h.host.queue_drain({ "\x1B"
                         "7\x1B[10;1HSTATUS\x1B"
                         "8" });
    h.host.pump();

    INFO("single-burst save/move/restore repaint should leave the visible cursor at the prompt");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
}

TEST_CASE("terminal cursor stays visible when repaint scope toggles cursor visibility", "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    h.host.queue_drain({ "\x1B"
                         "7\x1B[?25l\x1B[10;1HSTATUS" });
    h.host.pump();
    INFO("transient cursor hide inside a repaint scope should not blank the visible prompt cursor");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);

    h.host.queue_drain({ "\x1B"
                         "8\x1B[?25h" });
    h.host.pump();
    INFO("restoring the cursor and re-showing it should remain visually steady");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
}

TEST_CASE("terminal cursor ignores a prelude hide before a save-restore repaint burst", "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    h.host.queue_drain({ "\x1B[?25l" });
    h.host.pump();
    INFO("an early cursor-hide request should be debounced instead of blanking the prompt immediately");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);

    h.host.queue_drain({ "\x1B"
                         "7\x1B[10;1HSTATUS" });
    h.host.pump();
    INFO("once the repaint scope starts, the prompt cursor should stay pinned and visible");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);

    h.host.queue_drain({ "\x1B"
                         "8\x1B[?25h" });
    h.host.pump();
    INFO("restore plus show should remain visually steady");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
}

TEST_CASE("terminal cursor still hides after a standalone DECTCEM hide delay", "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    h.host.queue_drain({ "\x1B[?25l" });
    h.host.pump();
    INFO("standalone hide is deferred briefly to avoid repaint flicker");
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    handle->reset();
    h.host.pump();
    INFO("after the debounce window, the cursor should hide if the shell still wants it hidden");
    REQUIRE(handle->last_cursor.x == -1);
    REQUIRE(handle->last_cursor.y == -1);
}

TEST_CASE("terminal alternate screen hides cursor immediately on DECTCEM hide", "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    h.host.queue_drain({ "\x1B[?1049h\x1B[?25l" });
    h.host.pump();
    INFO("alt-screen apps should hide the cursor immediately instead of inheriting prompt debounce");
    REQUIRE(handle->last_cursor.x == -1);
    REQUIRE(handle->last_cursor.y == -1);
}

TEST_CASE("terminal alternate screen defers visible cursor moves until redraw settles", "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "\x1B[?1049h" });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 0);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    h.host.queue_drain({ "\x1B[10;10H" });
    h.host.pump();
    INFO("alt-screen cursor moves should stay hidden while redraw output is still settling");
    REQUIRE(handle->last_cursor.x == -1);
    REQUIRE(handle->last_cursor.y == -1);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    h.host.pump();
    INFO("the longer alt-screen settle delay should keep transient cursor hops hidden");
    REQUIRE(handle->last_cursor.x == -1);
    REQUIRE(handle->last_cursor.y == -1);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    handle->reset();
    h.host.pump();
    INFO("once the redraw settles, the cursor should appear at the final moved position");
    REQUIRE(handle->last_cursor.x == 9);
    REQUIRE(handle->last_cursor.y == 9);
}

TEST_CASE("terminal cursor publishes the final position immediately for an ordinary multi-chunk drain batch",
    "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h;
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();
    h.host.queue_drain({ "a", "b", "c" });
    h.host.pump();

    INFO("ordinary output batches should publish their settled final cursor without a synthetic hide pulse");
    REQUIRE(handle->last_cursor.x == 7);
    REQUIRE(handle->last_cursor.y == 0);
}

TEST_CASE("terminal synchronized output hides intermediate cursor churn until the batch settles",
    "[terminal][cursor]")
{
    TerminalCursorRefreshHarness h(120, 30);
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    h.host.queue_drain({ "PS> " });
    h.host.pump();
    REQUIRE(handle->last_cursor.x == 4);
    REQUIRE(handle->last_cursor.y == 0);
    handle->reset();

    const auto chunks = decode_capture_chunks({
        "G1s/MjAyNmg=",
        "G1s/MjVsG1sxMTsySBtbSxtbMm0bWzEyOzE4SHJ2G1syMm1lG1sxbRtbM0MoMRtbMjJtLxtbNzBDG1tLG1sxMzsySBtbSwobW0sbWzE1OzI3SBtbSxtbMTY7MkgbW0sbWzE3OzMySBtbSxtbMTI7MjdIG1s/MjVo",
        "G1s/MjAyNmw=",
        "G1s/MjVsG1sxNTszSBtbPzI1aA==",
    });

    for (const auto& chunk : chunks)
    {
        h.host.queue_drain({ chunk });
        h.host.pump();
    }

    INFO("status-row repaint chunks wrapped in synchronized output should not leak a visible cursor hop");
    REQUIRE(handle->last_cursor.x == -1);
    REQUIRE(handle->last_cursor.y == -1);

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    handle->reset();
    h.host.pump();
    INFO("once the synchronized batch settles, the prompt cursor should reappear at the final prompt position");
    REQUIRE(handle->last_cursor.x == 2);
    REQUIRE(handle->last_cursor.y == 14);
}

TEST_CASE("captured codex redraw burst reproduces model cursor hops without presenting the status-row cursor",
    "[terminal][cursor][capture]")
{
    TerminalCursorRefreshHarness h(120, 30);
    REQUIRE(h.ok);

    auto* handle = h.renderer.last_handle;
    REQUIRE(handle != nullptr);

    const auto chunks = decode_capture_chunks({
        "G1s/MjAyNmg=",
        "G1s/MjVsG1sxMTsySBtbSxtbMm0bWzEyOzE4SHJ2G1syMm1lG1sxbRtbM0MoMRtbMjJtLxtbNzBDG1tLG1sxMzsySBtbSwobW0sbWzE1OzI3SBtbSxtbMTY7MkgbW0sbWzE3OzMySBtbSxtbMTI7MjdIG1s/MjVo",
        "G1s/MjAyNmw=",
        "G1s/MjVsG1sxNTszSBtbPzI1aA==",
        "G1s/MjAyNmg=",
        "G1s/MjVsG1sxMTsySBtbSxtbMm0bWzEyOzIwSGUbWzIybXIbWzFtG1s0Qy8bWzIybTUbWzY5QxtbSxtbMTM7MkgbW0sKG1tLG1sxNTsyN0gbW0sbWzE2OzJIG1tLG1sxNzszMkgbW0sbWzEyOzI4SBtbPzI1aA==",
        "G1s/MjAyNmw=",
        "G1s/MjVsG1sxNTszSBtbPzI1aA==",
    });

    std::vector<glm::ivec2> presented_cursors;
    std::vector<glm::ivec2> vt_cursors;
    std::vector<bool> vt_visibility;
    presented_cursors.reserve(chunks.size());
    vt_cursors.reserve(chunks.size());
    vt_visibility.reserve(chunks.size());

    for (const auto& chunk : chunks)
    {
        h.host.queue_drain({ chunk });
        h.host.pump();
        presented_cursors.push_back(handle->last_cursor);
        vt_cursors.push_back({ h.host.vt_col(), h.host.vt_row() });
        vt_visibility.push_back(h.host.vt_cursor_visible());
    }

    INFO("presented cursors: " << cursor_points_to_string(presented_cursors));
    INFO("vt cursors: " << cursor_points_to_string(vt_cursors));

    bool saw_status_row = false;
    bool saw_prompt_row = false;
    bool presented_prompt_row = false;
    for (size_t i = 0; i < vt_cursors.size(); ++i)
    {
        if (!vt_visibility[i])
            continue;
        if (vt_cursors[i].y == 11)
            saw_status_row = true;
        if (vt_cursors[i].y == 14)
            saw_prompt_row = true;
        if (presented_cursors[i].y == 14)
            presented_prompt_row = true;
    }

    REQUIRE(saw_status_row);
    REQUIRE(saw_prompt_row);
    REQUIRE(presented_prompt_row);
}
