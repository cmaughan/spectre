#pragma once

// WI 25 -- canonical FakeGridHost for tests.
//
// A minimal concrete GridHostBase subclass that satisfies all pure-virtuals
// and exposes protected helpers (apply_grid_size, etc.) for test use.
//
// Use this instead of ad-hoc "StubGridHost" / "GuardedGridHost" classes
// scattered across individual test files.

#include <draxul/grid_host_base.h>

#include <string_view>

namespace draxul::tests
{

class FakeGridHost : public GridHostBase
{
public:
    // ---- GridHostBase pure-virtual overrides --------------------------------

    bool initialize_host() override
    {
        initialize_host_called = true;
        return !fail_initialize_host;
    }

    void on_viewport_changed() override
    {
        ++viewport_changed_calls;
    }

    void on_font_metrics_changed_impl() override
    {
        ++font_metrics_changed_calls;
    }

    std::string_view host_name() const override
    {
        return name;
    }

    // ---- IHost pure-virtual overrides not covered by GridHostBase -----------

    void pump() override
    {
        ++pump_calls;
    }

    void shutdown() override
    {
        ++shutdown_calls;
        running_ = false;
    }

    bool is_running() const override
    {
        return running_;
    }

    std::string init_error() const override
    {
        return {};
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override
    {
        running_ = false;
    }

    // ---- Expose protected helpers for direct test use ----------------------

    void exercise_apply_grid_size(int cols, int rows)
    {
        apply_grid_size(cols, rows);
    }

    void exercise_set_cursor_position(int col, int row)
    {
        set_cursor_position(col, row);
    }

    void exercise_set_cursor_position_preserve_blink(int col, int row)
    {
        set_cursor_position(col, row, CursorBlinkUpdate::Preserve);
    }

    void exercise_set_cursor_style(const CursorStyle& style, const BlinkTiming& timing,
        bool busy = false)
    {
        set_cursor_style(style, timing, busy);
    }

    bool exercise_advance_cursor_blink(std::chrono::steady_clock::time_point now)
    {
        return advance_cursor_blink(now);
    }

    // ---- Behaviour injection -----------------------------------------------

    bool fail_initialize_host = false;
    std::string name = "fake-grid-host";

    // ---- Call tracking -----------------------------------------------------

    bool initialize_host_called = false;
    int viewport_changed_calls = 0;
    int font_metrics_changed_calls = 0;
    int pump_calls = 0;
    int shutdown_calls = 0;

private:
    bool running_ = true;
};

} // namespace draxul::tests
