#include "support/fake_host.h"
#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>
#include <draxul/app_options.h>
#include <draxul/host.h>
#include <draxul/session_attach.h>

#include "app.h"
#include "session_state.h"

#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

class SessionAttachHost final : public FakeHost
{
public:
    SessionAttachHost()
        : FakeHost("session-attach")
    {
    }
};

SessionAttachHost* g_last_attach_host = nullptr;

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

std::unique_ptr<IHost> make_attach_host(HostKind /*kind*/)
{
    auto host = std::make_unique<SessionAttachHost>();
    g_last_attach_host = host.get();
    return host;
}

AppOptions make_attach_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false;
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    opts.override_display_ppi = 96.0f;
    opts.config_overrides.font_path = bundled_font_path();
    opts.renderer_create_fn = &make_fake_renderer;
    opts.host_factory = &make_attach_host;
    opts.host_kind = HostKind::PowerShell;
    opts.enable_session_attach = true;
    return opts;
}

} // namespace

TEST_CASE("session attach: no server reports no server", "[session_attach]")
{
    TempDir temp_dir("session-attach-no-server");
    HomeDirRedirect redirect(temp_dir.path);

    REQUIRE(SessionAttachServer::try_attach("default") == SessionAttachServer::AttachStatus::NoServer);
}

TEST_CASE("session attach: server accepts an attach request", "[session_attach]")
{
    TempDir temp_dir("session-attach-server");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    std::mutex mutex;
    std::condition_variable cv;
    int attach_count = 0;

    REQUIRE(server.start("alpha", [&]() {
        std::lock_guard lock(mutex);
        ++attach_count;
        cv.notify_one();
    }));

    REQUIRE(SessionAttachServer::try_attach("alpha") == SessionAttachServer::AttachStatus::Attached);

    std::unique_lock lock(mutex);
    REQUIRE(cv.wait_for(lock, std::chrono::milliseconds(500), [&]() { return attach_count == 1; }));

    server.stop();
}

TEST_CASE("session attach: shutdown command stops the server", "[session_attach]")
{
    TempDir temp_dir("session-attach-shutdown");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start("alpha", []() {}));
    REQUIRE(SessionAttachServer::send_command("alpha", SessionAttachServer::Command::Shutdown)
        == SessionAttachServer::AttachStatus::Attached);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (server.running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    REQUIRE_FALSE(server.running());
    server.stop();
}

TEST_CASE("session attach: detach command reaches the server", "[session_attach]")
{
    TempDir temp_dir("session-attach-detach");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    std::mutex mutex;
    std::condition_variable cv;
    int detach_count = 0;

    REQUIRE(server.start("alpha", [&](SessionAttachServer::Command command) {
        if (command != SessionAttachServer::Command::Detach)
            return;
        std::lock_guard lock(mutex);
        ++detach_count;
        cv.notify_one();
    }));

    REQUIRE(SessionAttachServer::send_command("alpha", SessionAttachServer::Command::Detach)
        == SessionAttachServer::AttachStatus::Attached);

    std::unique_lock lock(mutex);
    REQUIRE(cv.wait_for(lock, std::chrono::milliseconds(500), [&]() { return detach_count == 1; }));

    server.stop();
}

TEST_CASE("session attach: different session ids stay isolated", "[session_attach]")
{
    TempDir temp_dir("session-attach-isolated");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start("workbench", []() {}));

    REQUIRE(SessionAttachServer::probe("workbench") == SessionAttachServer::ProbeStatus::Running);
    REQUIRE(SessionAttachServer::try_attach("other") == SessionAttachServer::AttachStatus::NoServer);

    server.stop();
}

TEST_CASE("session attach: probe reports a running session server", "[session_attach]")
{
    TempDir temp_dir("session-attach-probe");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start("alpha", []() {}));
    REQUIRE(SessionAttachServer::probe("alpha") == SessionAttachServer::ProbeStatus::Running);
    REQUIRE(SessionAttachServer::probe("beta") == SessionAttachServer::ProbeStatus::NoServer);
    server.stop();
}

TEST_CASE("session attach: live-session query returns server summary", "[session_attach]")
{
    TempDir temp_dir("session-attach-query");
    HomeDirRedirect redirect(temp_dir.path);

    SessionAttachServer server;
    REQUIRE(server.start(
        "alpha",
        [](SessionAttachServer::Command) {},
        []() {
            SessionAttachServer::LiveSessionInfo info;
            info.workspace_count = 2;
            info.pane_count = 5;
            info.detached = true;
            info.owner_pid = 4242;
            info.last_attached_unix_s = 111;
            info.last_detached_unix_s = 222;
            return info;
        }));

    SessionAttachServer::LiveSessionInfo info;
    REQUIRE(SessionAttachServer::query_live_session("alpha", &info));
    REQUIRE(info.workspace_count == 2);
    REQUIRE(info.pane_count == 5);
    REQUIRE(info.detached);
    REQUIRE(info.owner_pid == 4242);
    REQUIRE(info.last_attached_unix_s == 111);
    REQUIRE(info.last_detached_unix_s == 222);

    server.stop();
}

TEST_CASE("app session attach: close request detaches a shell session", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-attach");
    HomeDirRedirect redirect(temp_dir.path);

    FakeWindow* created_window = nullptr;
    g_last_attach_host = nullptr;

    AppOptions opts = make_attach_options();
    opts.window_factory = [&]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    REQUIRE(g_last_attach_host != nullptr);
    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));

    created_window->queue_close_request();
    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));
    REQUIRE_FALSE(created_window->is_visible());
    auto metadata = load_session_runtime_metadata("default");
    REQUIRE(metadata);
    REQUIRE(metadata->live);
    REQUIRE(metadata->detached);
    SessionAttachServer::LiveSessionInfo live_info;
    REQUIRE(SessionAttachServer::query_live_session("default", &live_info));
    REQUIRE(live_info.workspace_count == 1);
    REQUIRE(live_info.pane_count == 1);
    REQUIRE(live_info.detached);
    REQUIRE(g_last_attach_host->shutdown_calls == 0);
    REQUIRE(g_last_attach_host->request_close_calls == 0);

    app.shutdown();
}

TEST_CASE("app session attach: shutdown command kills the session", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-shutdown");
    HomeDirRedirect redirect(temp_dir.path);

    g_last_attach_host = nullptr;

    App app(make_attach_options());
    REQUIRE(app.initialize());
    REQUIRE(g_last_attach_host != nullptr);
    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));

    REQUIRE(SessionAttachServer::send_command("default", SessionAttachServer::Command::Shutdown)
        == SessionAttachServer::AttachStatus::Attached);
    app.run();
    REQUIRE(g_last_attach_host->request_close_calls == 1);
    REQUIRE_FALSE(load_session_runtime_metadata("default").has_value());

    app.shutdown();
}

TEST_CASE("app session attach: detach command hides the session window", "[session_attach][app]")
{
    const std::string font = bundled_font_path();
    if (!std::filesystem::exists(font))
        SKIP("bundled font not found");

    TempDir temp_dir("app-session-explicit-detach");
    HomeDirRedirect redirect(temp_dir.path);

    FakeWindow* created_window = nullptr;
    g_last_attach_host = nullptr;

    AppOptions opts = make_attach_options();
    opts.window_factory = [&]() {
        auto window = std::make_unique<FakeWindow>();
        created_window = window.get();
        return window;
    };

    App app(std::move(opts));
    REQUIRE(app.initialize());
    REQUIRE(created_window != nullptr);
    REQUIRE(app.run_smoke_test(std::chrono::milliseconds(200)));

    REQUIRE(SessionAttachServer::send_command("default", SessionAttachServer::Command::Detach)
        == SessionAttachServer::AttachStatus::Attached);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (created_window->is_visible() && std::chrono::steady_clock::now() < deadline)
        REQUIRE(app.run_smoke_test(std::chrono::milliseconds(50)));
    REQUIRE_FALSE(created_window->is_visible());

    SessionAttachServer::LiveSessionInfo live_info;
    REQUIRE(SessionAttachServer::query_live_session("default", &live_info));
    REQUIRE(live_info.detached);

    app.shutdown();
}
