// WI 120: ToastHost lifecycle tests.
//
// Covers stacking, expiry, fade, request_frame() bookkeeping, early-buffer
// replay (push before initialize), and the enable_toast_notifications gate.
// A fake clock is injected via ToastHost::set_time_source so timing can be
// advanced without real sleeps.

#include <catch2/catch_test_macros.hpp>

#include "toast_host.h"

#include <chrono>
#include <draxul/app_config.h>
#include <draxul/gui/toast_renderer.h>
#include <draxul/text_service.h>
#include <filesystem>

#include "support/fake_grid_pipeline_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

using namespace draxul;
using namespace std::chrono_literals;

namespace
{

std::filesystem::path bundled_font_path()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

bool init_text_service(TextService& text_service)
{
    const auto font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        return false;

    TextServiceConfig config;
    config.font_path = font_path.string();
    return text_service.initialize(config, TextService::DEFAULT_POINT_SIZE, 96.0f);
}

// Monotonic fake clock that only advances when tests ask it to.
struct FakeClock
{
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::time_point{}
        + std::chrono::seconds(1000);

    void advance(std::chrono::milliseconds dt)
    {
        now += dt;
    }

    ToastHost::TimeSource source()
    {
        return [this] { return now; };
    }
};

struct ToastHostHarness
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    ToastHost host;
    FakeClock clock;

    bool init()
    {
        host.set_time_source(clock.source());

        if (!init_text_service(text_service))
            return false;

        HostViewport viewport;
        viewport.pixel_size = { window.pixel_w_, window.pixel_h_ };
        viewport.grid_size = { 1, 1 };

        HostContext context{
            .window = &window,
            .grid_renderer = &renderer,
            .text_service = &text_service,
            .initial_viewport = viewport,
            .display_ppi = window.display_ppi_,
        };
        return host.initialize(context, callbacks);
    }
};

} // namespace

// ── Stacking ───────────────────────────────────────────────────────────────

TEST_CASE("ToastHost: pushing three toasts makes all three active", "[toast][lifecycle]")
{
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    h.host.push(gui::ToastLevel::Info, "first", 4.0f);
    h.host.push(gui::ToastLevel::Warn, "second", 4.0f);
    h.host.push(gui::ToastLevel::Error, "third", 4.0f);
    h.host.pump();

    const auto& active = h.host.active_toasts_for_test();
    REQUIRE(active.size() == 3);
    CHECK(active[0].message == "first");
    CHECK(active[1].message == "second");
    CHECK(active[2].message == "third");
    CHECK(active[0].level == gui::ToastLevel::Info);
    CHECK(active[1].level == gui::ToastLevel::Warn);
    CHECK(active[2].level == gui::ToastLevel::Error);
}

TEST_CASE("ToastHost: no stacking cap — many toasts all appear in order", "[toast][lifecycle]")
{
    // Current policy: no hard stacking cap. render_toasts truncates visually
    // once it runs out of rows, but the model itself keeps them all active.
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    for (int i = 0; i < 10; ++i)
        h.host.push(gui::ToastLevel::Info, "msg", 4.0f);
    h.host.pump();

    CHECK(h.host.active_toasts_for_test().size() == 10);
}

// ── Expiry ─────────────────────────────────────────────────────────────────

TEST_CASE("ToastHost: short-duration toast expires after clock advances past it",
    "[toast][lifecycle]")
{
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    h.host.push(gui::ToastLevel::Info, "bye", 0.1f);
    h.host.pump();
    REQUIRE(h.host.active_toasts_for_test().size() == 1);

    // Advance past the duration. On the next pump the toast should be erased.
    h.clock.advance(200ms);
    h.host.pump();
    CHECK(h.host.active_toasts_for_test().empty());
}

TEST_CASE("ToastHost: zero-duration toast is discarded on the next pump",
    "[toast][lifecycle]")
{
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    h.host.push(gui::ToastLevel::Info, "poof", 0.0f);
    h.host.pump();
    // Policy: the toast is inserted with remaining_s=0, ticked with ~0 dt
    // (because last_tick_ is reset when no toasts were previously active),
    // and then erased because remaining_s <= 0 after the tick.
    CHECK(h.host.active_toasts_for_test().empty());
}

TEST_CASE("ToastHost: negative-duration toast is discarded on the next pump",
    "[toast][lifecycle]")
{
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    h.host.push(gui::ToastLevel::Info, "poof", -1.0f);
    h.host.pump();
    CHECK(h.host.active_toasts_for_test().empty());
}

// ── Fade (pure function on render_toasts) ─────────────────────────────────

TEST_CASE("render_toasts: fade kicks in during the final half-second", "[toast][fade]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    // Helper to build a single-toast state with a given remaining_s.
    auto alpha_at_remaining = [&](float remaining) {
        gui::ToastEntry entry{
            gui::ToastLevel::Info,
            std::string("hello"),
            remaining,
            4.0f, // total_s, comfortably > kFadeDuration (0.5f)
        };
        std::array<gui::ToastEntry, 1> entries{ entry };
        gui::ToastViewState vs;
        vs.grid_cols = 120;
        vs.grid_rows = 30;
        vs.entries = entries;
        auto cells = gui::render_toasts(vs, text_service);
        REQUIRE_FALSE(cells.empty());
        // Return the bg alpha of the first emitted cell (toast background fill).
        return cells.front().bg.a;
    };

    const float full_alpha = alpha_at_remaining(2.0f); // well outside fade window
    const float mid_fade = alpha_at_remaining(0.25f); // halfway through 0.5s fade
    const float near_end = alpha_at_remaining(0.05f); // almost invisible

    // Full opacity phase uses the configured bg alpha (0.92 for Info).
    CHECK(full_alpha > 0.9f);
    // During fade, alpha monotonically decreases.
    CHECK(mid_fade < full_alpha);
    CHECK(near_end < mid_fade);
    // And stays non-negative.
    CHECK(near_end >= 0.0f);
}

TEST_CASE("ToastHost: toast is fully removed once remaining_s reaches zero",
    "[toast][fade]")
{
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    h.host.push(gui::ToastLevel::Info, "bye", 1.0f);
    h.host.pump();
    REQUIRE(h.host.active_toasts_for_test().size() == 1);

    // Advance to exactly the expiry boundary and pump.
    h.clock.advance(1000ms);
    h.host.pump();
    CHECK(h.host.active_toasts_for_test().empty());
}

// ── request_frame() bookkeeping ───────────────────────────────────────────

TEST_CASE("ToastHost: request_frame() is called each pump while toasts are active",
    "[toast][request_frame]")
{
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    const int before = h.callbacks.request_frame_calls;
    h.host.push(gui::ToastLevel::Info, "hello", 4.0f);

    h.host.pump();
    const int after_first = h.callbacks.request_frame_calls;
    CHECK(after_first > before);

    // A second pump with the toast still active should also request a frame
    // (needed so the fade animation keeps advancing).
    h.clock.advance(16ms);
    h.host.pump();
    CHECK(h.callbacks.request_frame_calls > after_first);
}

TEST_CASE("ToastHost: request_frame() is NOT called when no toasts remain",
    "[toast][request_frame]")
{
    ToastHostHarness h;
    if (!h.init())
        SKIP("bundled font not found");

    // No toasts pushed yet — a pump should be a total no-op.
    const int baseline = h.callbacks.request_frame_calls;
    h.host.pump();
    CHECK(h.callbacks.request_frame_calls == baseline);

    // Push and expire a toast, then pump again with the active list empty.
    h.host.push(gui::ToastLevel::Info, "x", 0.1f);
    h.host.pump();
    h.clock.advance(200ms);
    h.host.pump();
    REQUIRE(h.host.active_toasts_for_test().empty());

    const int after_expiry = h.callbacks.request_frame_calls;
    h.host.pump();
    CHECK(h.callbacks.request_frame_calls == after_expiry);
}

// ── Early-buffer replay (push before initialize) ─────────────────────────

TEST_CASE("ToastHost: toasts pushed before initialize() surface on the first pump",
    "[toast][early-buffer]")
{
    ToastHostHarness h;
    // Push BEFORE calling h.init() (and therefore before host.initialize()).
    h.host.push(gui::ToastLevel::Info, "early", 4.0f);
    h.host.push(gui::ToastLevel::Warn, "early2", 4.0f);

    if (!h.init())
        SKIP("bundled font not found");

    // The pending queue survives initialize(); the first pump should drain
    // it into the active list so the buffered toasts become visible.
    h.host.pump();
    const auto& active = h.host.active_toasts_for_test();
    REQUIRE(active.size() == 2);
    CHECK(active[0].message == "early");
    CHECK(active[1].message == "early2");
}

// ── enable_toast_notifications gate ───────────────────────────────────────

TEST_CASE("AppConfig: enable_toast_notifications defaults to true", "[toast][config]")
{
    const AppConfig config = AppConfig::parse("");
    CHECK(config.enable_toast_notifications);
}

TEST_CASE("AppConfig: enable_toast_notifications = false is honoured by the parser",
    "[toast][config]")
{
    // The App-layer push_toast() gate drops all toasts when this is false;
    // the ToastHost itself is never called. Verify the config round-trip so
    // the gate has something reliable to read.
    const AppConfig config = AppConfig::parse("enable_toast_notifications = false\n");
    CHECK_FALSE(config.enable_toast_notifications);
}
