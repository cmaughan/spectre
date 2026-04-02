// app_smoke_tests.cpp
//
// End-to-end orchestrator smoke tests: exercises App::initialize() ->
// App::pump_once() x N -> App::shutdown() using FakeWindow, FakeRenderer,
// and a minimal FakeHost stub injected via AppOptions::host_factory.
//
// These tests complement app_pump_tests.cpp (which focuses on failure
// rollback paths) by covering the happy-path lifecycle.

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"
#include "support/test_host_callbacks.h"

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <draxul/host.h>
#include <filesystem>
#include <fstream>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

// ---------------------------------------------------------------------------
// Minimal IHost stub — does nothing but report itself as running.
// This lets App::initialize() succeed through the host step without
// spawning a real process or requiring a pty / pipe.
// ---------------------------------------------------------------------------
class SmokeTestHost : public IHost
{
public:
    bool initialize(const HostContext&, IHostCallbacks& callbacks) override
    {
        callbacks_ = &callbacks;
        initialized_ = true;
        return true;
    }

    void shutdown() override
    {
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

    void set_viewport(const HostViewport&) override {}

    void pump() override
    {
        ++pump_count_;
        // Simulate what a real host does: request a frame after producing output.
        // Without this, the pump loop would block in wait_events forever.
        if (running_ && callbacks_)
            callbacks_->request_frame();
    }

    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override
    {
        running_ = false;
    }

    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    HostRuntimeState runtime_state() const override
    {
        HostRuntimeState state;
        state.content_ready = initialized_;
        return state;
    }

    HostDebugState debug_state() const override
    {
        HostDebugState state;
        state.name = "smoke-test";
        return state;
    }

    // Introspection for test assertions.
    int pump_count() const
    {
        return pump_count_;
    }
    bool was_initialized() const
    {
        return initialized_;
    }

private:
    IHostCallbacks* callbacks_ = nullptr;
    bool initialized_ = false;
    bool running_ = true;
    int pump_count_ = 0;
};

// A host that fails to initialize — used by the "host init fails" test case.
class FailingInitHost final : public SmokeTestHost
{
public:
    bool initialize(const HostContext&, IHostCallbacks&) override
    {
        return false;
    }
    std::string init_error() const override
    {
        return "deliberate test failure";
    }
};

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

std::filesystem::path canonical_path(const std::filesystem::path& path)
{
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : canonical;
}

// Shared pointers to the most recently created fakes — lets tests inspect
// renderer/host state after App takes ownership.
SmokeTestHost* g_last_smoke_host = nullptr;
FakeTermRenderer* g_last_fake_renderer = nullptr;
class ReloadTrackingHost;
ReloadTrackingHost* g_last_reload_host = nullptr;

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    auto renderer = std::make_unique<FakeTermRenderer>();
    g_last_fake_renderer = renderer.get();
    return RendererBundle{ std::move(renderer) };
}

std::unique_ptr<IHost> make_smoke_host(HostKind /*kind*/)
{
    auto host = std::make_unique<SmokeTestHost>();
    g_last_smoke_host = host.get();
    return host;
}

class InitFrameOnlyHost final : public IHost
{
public:
    bool initialize(const HostContext&, IHostCallbacks& callbacks) override
    {
        callbacks_ = &callbacks;
        initialized_ = true;
        callbacks_->request_frame();
        return true;
    }

    void shutdown() override
    {
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

    void set_viewport(const HostViewport&) override {}

    void pump() override
    {
        ++pump_count_;
    }

    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override
    {
        running_ = false;
    }

    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    HostRuntimeState runtime_state() const override
    {
        HostRuntimeState state;
        state.content_ready = initialized_;
        return state;
    }

    HostDebugState debug_state() const override
    {
        HostDebugState state;
        state.name = "init-frame-only";
        return state;
    }

    int pump_count() const
    {
        return pump_count_;
    }

private:
    IHostCallbacks* callbacks_ = nullptr;
    bool initialized_ = false;
    bool running_ = true;
    int pump_count_ = 0;
};

class ReloadTrackingHost final : public SmokeTestHost
{
public:
    void on_config_reloaded(const HostReloadConfig& config) override
    {
        ++reload_count_;
        last_config_ = config;
    }

    void on_font_metrics_changed() override
    {
        ++font_metrics_changed_count_;
    }

    void set_imgui_font(const std::string&, float) override
    {
        ++imgui_font_update_count_;
    }

    void reset_tracking()
    {
        reload_count_ = 0;
        font_metrics_changed_count_ = 0;
        imgui_font_update_count_ = 0;
        last_config_ = HostReloadConfig{};
    }

    int reload_count() const
    {
        return reload_count_;
    }

    int font_metrics_changed_count() const
    {
        return font_metrics_changed_count_;
    }

    const HostReloadConfig& last_config() const
    {
        return last_config_;
    }

private:
    int reload_count_ = 0;
    int font_metrics_changed_count_ = 0;
    int imgui_font_update_count_ = 0;
    HostReloadConfig last_config_;
};

// Constructs AppOptions for a fully-initializable App with all fakes.
AppOptions make_smoke_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    opts.override_display_ppi = 96.0f;
    opts.config_overrides.font_path = bundled_font_path();
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.host_factory = &make_smoke_host;
    opts.host_kind = HostKind::Nvim; // value is irrelevant — factory ignores it
    return opts;
}

struct CurrentDirGuard
{
    std::filesystem::path original;

    explicit CurrentDirGuard(std::filesystem::path cwd)
        : original(std::move(cwd))
    {
    }

    ~CurrentDirGuard()
    {
        std::error_code ec;
        std::filesystem::current_path(original, ec);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Happy path: full lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: initialize succeeds with all fakes", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());

    REQUIRE(app.initialize());
    REQUIRE(app.init_error().empty());
    REQUIRE(g_last_smoke_host != nullptr);
    REQUIRE(g_last_smoke_host->was_initialized());

    app.shutdown();
}

TEST_CASE("app smoke: initialize does not mutate cwd when using bundled font fallback",
    "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    const auto original_cwd = std::filesystem::current_path();
    auto temp_cwd = std::filesystem::temp_directory_path() / "draxul-cwd-stability";
    std::error_code ec;
    std::filesystem::remove_all(temp_cwd, ec);
    std::filesystem::create_directories(temp_cwd);
    std::filesystem::current_path(temp_cwd);
    CurrentDirGuard guard(original_cwd);

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());
    REQUIRE(canonical_path(std::filesystem::current_path()) == canonical_path(temp_cwd));

    app.shutdown();
    REQUIRE(canonical_path(std::filesystem::current_path()) == canonical_path(temp_cwd));
}

TEST_CASE("app smoke: pump_once runs without crash after successful init", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());

    // Run the smoke test helper which internally calls pump_once in a loop.
    // With a fake host that reports content_ready immediately, the smoke test
    // should complete well before the timeout.
    const bool smoke_ok = app.run_smoke_test(std::chrono::milliseconds(2000));
    REQUIRE(smoke_ok);

    // The host should have been pumped at least once during the smoke test loop.
    REQUIRE(g_last_smoke_host != nullptr);
    REQUIRE(g_last_smoke_host->pump_count() > 0);

    app.shutdown();
}

TEST_CASE("app smoke: shutdown after successful init is clean", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());

    app.shutdown();
    // Second shutdown must not crash (idempotency).
    app.shutdown();
}

TEST_CASE("app smoke: initial frame renders before any later host redraw", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_fake_renderer = nullptr;
    AppOptions opts = make_smoke_options();
    opts.show_diagnostics_on_startup = true;
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        return std::make_unique<InitFrameOnlyHost>();
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(g_last_fake_renderer != nullptr);

    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));
    REQUIRE(g_last_fake_renderer->render_imgui_calls > 0);

    app.shutdown();
}

// ---------------------------------------------------------------------------
// Failure path: host factory returns nullptr
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: initialization fails when host factory returns nullptr", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    AppOptions opts = make_smoke_options();
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> { return nullptr; };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE_FALSE(app.init_error().empty());
    // Destructor runs — must not crash.
}

// ---------------------------------------------------------------------------
// Failure path: host initialize() returns false
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: initialization fails when host init fails", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    AppOptions opts = make_smoke_options();
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        return std::make_unique<FailingInitHost>();
    };

    App app(std::move(opts));
    REQUIRE_FALSE(app.initialize());
    REQUIRE_FALSE(app.init_error().empty());
    // Shutdown after failed init must not crash.
    app.shutdown();
}

// ---------------------------------------------------------------------------
// Host dies mid-pump — App exits gracefully
// ---------------------------------------------------------------------------

TEST_CASE("app smoke: host death during pump_once causes clean exit", "[app_smoke]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    g_last_smoke_host = nullptr;
    App app(make_smoke_options());
    REQUIRE(app.initialize());
    REQUIRE(g_last_smoke_host != nullptr);

    // Simulate host process death.
    g_last_smoke_host->shutdown();

    // run_smoke_test pumps until content_ready + saw_frame, or timeout.
    // With a dead host, it should time out (return false) rather than crash.
    const bool smoke_ok = app.run_smoke_test(std::chrono::milliseconds(200));
    REQUIRE_FALSE(smoke_ok);

    app.shutdown();
}

TEST_CASE("app smoke: reload_config action reloads user config from disk", "[app_smoke][config]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp("draxul-reload-config");
    HomeDirRedirect redir(temp.path);
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 11.0\n"
               "palette_bg_alpha = 0.9\n"
               "smooth_scroll = true\n"
               "scroll_speed = 1.0\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n";
    }

    FakeWindow* created_window = nullptr;
    g_last_reload_host = nullptr;

    AppOptions opts = make_smoke_options();
    opts.load_user_config = true;
    opts.save_user_config = false;
    opts.window_factory = [&created_window]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };
    opts.host_factory = [](HostKind) -> std::unique_ptr<IHost> {
        auto host = std::make_unique<ReloadTrackingHost>();
        g_last_reload_host = host.get();
        return host;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    REQUIRE(g_last_reload_host != nullptr);
    g_last_reload_host->reset_tracking();

    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "font_size = 14.5\n"
               "palette_bg_alpha = 0.4\n"
               "smooth_scroll = false\n"
               "scroll_speed = 2.5\n"
               "[keybindings]\n"
               "reload_config = \"Ctrl+Alt+R\"\n";
    }

    REQUIRE(created_window->on_key != nullptr);
    created_window->on_key(KeyEvent{ 0, SDLK_R, kModCtrl | kModAlt, true });

    REQUIRE(g_last_reload_host->reload_count() == 1);
    REQUIRE(g_last_reload_host->font_metrics_changed_count() > 0);
    CHECK(g_last_reload_host->last_config().font_size == Catch::Approx(14.5f));
    CHECK(g_last_reload_host->last_config().palette_bg_alpha == Catch::Approx(0.4f));
    CHECK(g_last_reload_host->last_config().smooth_scroll == false);
    CHECK(g_last_reload_host->last_config().scroll_speed == Catch::Approx(2.5f));

    app.shutdown();
}
