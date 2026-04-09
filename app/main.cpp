#include "app.h"
#include "cli_args.h"
#include "session_state.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <draxul/bmp.h>
#include <draxul/host_registry.h>
#include <draxul/log.h>
#include <draxul/nanovg_demo_host.h>
#include <draxul/perf_timing.h>
#include <draxul/session_attach.h>
#ifdef DRAXUL_ENABLE_MEGACITY
#include <draxul/megacity_host.h>
#endif
#ifdef DRAXUL_ENABLE_RENDER_TESTS
#include <draxul/render_test.h>
#endif
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdio>
#include <windows.h>

// This must come after shellapi!
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace
{

#ifdef _WIN32
void ensure_console_io(bool allow_alloc_console)
{
    static bool configured = false;
    if (configured)
        return;

    if (!GetConsoleWindow())
    {
        if (!AttachConsole(ATTACH_PARENT_PROCESS) && allow_alloc_console)
            AllocConsole();
    }

    if (GetConsoleWindow())
    {
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
        freopen_s(&stream, "CONIN$", "r", stdin);
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
        configured = true;
    }
}

std::vector<std::string> command_line_args()
{
    PERF_MEASURE();
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return args;

    for (int i = 0; i < argc; ++i)
    {
        int size = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size <= 1)
        {
            args.emplace_back();
            continue;
        }
        std::string converted(size, '\0');
        int written = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, converted.data(), size, nullptr, nullptr);
        if (written <= 0)
        {
            args.emplace_back();
            continue;
        }
        converted.pop_back(); // remove trailing NUL embedded by WideCharToMultiByte
        args.push_back(std::move(converted));
    }
    LocalFree(argv);
    return args;
}
#else
std::vector<std::string> command_line_args(int argc, char* argv[])
{
    PERF_MEASURE();
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
        args.emplace_back(argv[i]);
    return args;
}
#endif

// CLI parsing has moved to app/cli_args.{h,cpp} so it can be unit-tested
// without spawning a subprocess. See draxul::parse_args() / ParseArgsResult.

std::filesystem::path executable_dir(const std::vector<std::string>& args)
{
    PERF_MEASURE();
#ifdef _WIN32
    std::wstring exe_path(MAX_PATH, L'\0');
    for (;;)
    {
        DWORD size = GetModuleFileNameW(nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()));
        if (size == 0)
            return {};
        if (size < exe_path.size())
        {
            exe_path.resize(size);
            break;
        }
        exe_path.resize(exe_path.size() * 2);
    }
    return std::filesystem::path(exe_path).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};

    std::string exe_path(size, '\0');
    if (_NSGetExecutablePath(exe_path.data(), &size) != 0)
        return {};
    exe_path.resize(std::char_traits<char>::length(exe_path.c_str()));
    return std::filesystem::path(exe_path).parent_path();
#elif defined(__linux__)
    std::vector<char> exe_path(256, '\0');
    for (;;)
    {
        const ssize_t size = readlink("/proc/self/exe", exe_path.data(), exe_path.size());
        if (size < 0)
            break;
        if (static_cast<size_t>(size) < exe_path.size())
            return std::filesystem::path(std::string(exe_path.data(), static_cast<size_t>(size))).parent_path();
        exe_path.resize(exe_path.size() * 2);
    }
    if (args.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::path(args.front())).parent_path();
#else
    if (args.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::path(args.front())).parent_path();
#endif
}

} // namespace

static int draxul_main(std::vector<std::string> args)
{
    PERF_MEASURE();
    auto parse_result = draxul::parse_args(args);
#ifdef _WIN32
    const bool needs_console_output = parse_result.error.has_value()
        || parse_result.args.want_console
        || parse_result.args.list_sessions
        || parse_result.args.attach_session
        || parse_result.args.kill_session;
    if (needs_console_output)
        ensure_console_io(true);
#endif
    if (parse_result.error)
    {
        std::fprintf(stderr, "%s\n", parse_result.error->c_str());
        return 1;
    }
    draxul::ParsedArgs& parsed = parse_result.args;

    draxul::configure_default_logging();

    // Register host providers. The terminal product registers its built-ins
    // unconditionally; optional modules (megacity) only register when their
    // build flag is on. Nothing in draxul-app or draxul-host links the
    // megacity library — this main.cpp file is the sole megacity touchpoint
    // in the executable.
    auto& host_registry = draxul::HostProviderRegistry::global();
    host_registry.clear();
    draxul::register_builtin_host_providers(host_registry);
    draxul::register_nanovg_demo_host_provider(host_registry);
#ifdef DRAXUL_ENABLE_MEGACITY
    draxul::register_megacity_host_provider(host_registry);
#endif

    // CLI overrides for logging — these always work, unlike env vars which
    // may not propagate to macOS .app bundles.
    if (!parsed.log_file.empty() || !parsed.log_level.empty())
    {
        draxul::LogOptions log_overrides;
        log_overrides.min_level = parsed.log_level.empty()
            ? draxul::LogLevel::Debug
            : draxul::parse_log_level_or(parsed.log_level, draxul::LogLevel::Debug);
        log_overrides.enable_stderr = true;
        log_overrides.enable_file = !parsed.log_file.empty();
        log_overrides.file_path = parsed.log_file;
        draxul::configure_logging(log_overrides);
    }

    if (parsed.list_sessions)
    {
        std::string list_error;
        const auto sessions = draxul::list_saved_sessions(&list_error);
        if (!list_error.empty())
        {
            std::fprintf(stderr, "Failed to list sessions: %s\n", list_error.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        if (sessions.empty())
        {
            std::printf("No saved sessions.\n");
            draxul::shutdown_logging();
            return 0;
        }

        for (const auto& session : sessions)
        {
            const auto probe_status = draxul::SessionAttachServer::probe(session.session_id);
            const char* state = "saved";
            if (probe_status == draxul::SessionAttachServer::ProbeStatus::Running)
                state = "live";
            else if (probe_status == draxul::SessionAttachServer::ProbeStatus::Error)
                state = "saved?";

            std::printf("%s\t%s\t%d workspace%s\t%d pane%s\n",
                session.session_id.c_str(),
                state,
                session.workspace_count,
                session.workspace_count == 1 ? "" : "s",
                session.pane_count,
                session.pane_count == 1 ? "" : "s");
        }
        draxul::shutdown_logging();
        return 0;
    }

    if (parsed.attach_session)
    {
        std::string attach_error;
        const auto attach_status = draxul::SessionAttachServer::try_attach(
            parsed.session_id, &attach_error);
        if (attach_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            std::printf("Attached to session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 0;
        }
        if (attach_status == draxul::SessionAttachServer::AttachStatus::NoServer)
        {
            std::fprintf(stderr, "No running session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        std::fprintf(stderr, "Failed to attach to session '%s': %s\n",
            parsed.session_id.c_str(), attach_error.empty() ? "unknown error" : attach_error.c_str());
        draxul::shutdown_logging();
        return 1;
    }

    if (parsed.kill_session)
    {
        std::string command_error;
        const auto kill_status = draxul::SessionAttachServer::send_command(
            parsed.session_id, draxul::SessionAttachServer::Command::Shutdown, &command_error);
        if (kill_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            std::printf("Killed running session '%s'.\n", parsed.session_id.c_str());
            (void)draxul::delete_session_state(parsed.session_id);
            draxul::shutdown_logging();
            return 0;
        }

        std::string delete_error;
        const bool deleted_saved_state = draxul::delete_session_state(parsed.session_id, &delete_error);
        if (kill_status == draxul::SessionAttachServer::AttachStatus::NoServer && deleted_saved_state)
        {
            std::printf("Deleted saved session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 0;
        }

        if (kill_status == draxul::SessionAttachServer::AttachStatus::NoServer)
        {
            std::fprintf(stderr, "No running or saved session '%s'.\n", parsed.session_id.c_str());
            draxul::shutdown_logging();
            return 1;
        }

        std::fprintf(stderr, "Failed to kill session '%s': %s\n",
            parsed.session_id.c_str(), command_error.empty() ? "unknown error" : command_error.c_str());
        draxul::shutdown_logging();
        return 1;
    }

#ifdef DRAXUL_ENABLE_RENDER_TESTS
    std::optional<draxul::RenderTestScenario> render_test;
#endif
    draxul::AppOptions options;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (!parsed.render_test_path.empty())
    {
        std::string load_error;
        render_test = draxul::load_render_test_scenario(parsed.render_test_path, &load_error);
        if (!render_test)
        {
            DRAXUL_LOG_ERROR(draxul::LogCategory::App, "%s", load_error.c_str());
            draxul::shutdown_logging();
            return 1;
        }
        options = render_test->make_app_options();
        options.show_render_test_window = parsed.show_render_test_window;
    }
    else if (parsed.smoke_test)
#else
    if (parsed.smoke_test)
#endif
    {
        options.activate_window_on_startup = false;
    }

    if (parsed.host_kind)
        options.host_kind = *parsed.host_kind;
    if (!parsed.host_command.empty())
        options.host_command = parsed.host_command;
    if (!parsed.host_source_path.empty())
        options.host_source_path = parsed.host_source_path.string();
    if (parsed.continuous_refresh)
        options.request_continuous_refresh = true;
    if (parsed.no_vblank)
        options.no_vblank = true;
    if (parsed.no_ui)
        options.hide_host_ui_panels = true;
    if (!parsed.screenshot_path.empty())
    {
        if (parsed.screenshot_width > 0 && parsed.screenshot_height > 0)
        {
            options.config_overrides.window_width = parsed.screenshot_width;
            options.config_overrides.window_height = parsed.screenshot_height;
            options.render_target_pixel_width = parsed.screenshot_width;
            options.render_target_pixel_height = parsed.screenshot_height;
        }
        options.save_user_config = false;
    }

    const auto exe_dir = executable_dir(args);
    if (!options.config_overrides.font_path.has_value() && !exe_dir.empty())
    {
        const auto bundled_font = exe_dir / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
        if (std::filesystem::exists(bundled_font))
            options.config_overrides.font_path = bundled_font.string();
    }
    options.session_id = parsed.session_id;

#ifdef DRAXUL_ENABLE_RENDER_TESTS
    const bool allow_session_attach = !parsed.smoke_test
        && !render_test.has_value()
        && parsed.screenshot_path.empty();
#else
    const bool allow_session_attach = !parsed.smoke_test
        && parsed.screenshot_path.empty();
#endif
    if (allow_session_attach)
    {
        std::string attach_error;
        const auto attach_status = draxul::SessionAttachServer::try_attach(
            parsed.session_id, &attach_error);
        if (attach_status == draxul::SessionAttachServer::AttachStatus::Attached)
        {
            draxul::shutdown_logging();
            return 0;
        }
        if (attach_status == draxul::SessionAttachServer::AttachStatus::Error
            && !attach_error.empty())
        {
            DRAXUL_LOG_WARN(draxul::LogCategory::App,
                "Session attach probe failed: %s", attach_error.c_str());
        }
    }
    options.enable_session_attach = allow_session_attach;

    draxul::App app(std::move(options));

    if (!app.initialize())
    {
        DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to initialize draxul: %s", app.init_error().c_str());
#ifdef DRAXUL_ENABLE_RENDER_TESTS
        const bool ci_mode = parsed.smoke_test || !parsed.render_test_path.empty() || !parsed.screenshot_path.empty();
#else
        const bool ci_mode = parsed.smoke_test || !parsed.screenshot_path.empty();
#endif
        if (!ci_mode)
        {
            const std::string& reason = app.init_error();
            const char* message = reason.empty()
                ? "Draxul failed to start. Check the log for details."
                : reason.c_str();
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Draxul \xe2\x80\x94 Startup Error", message, nullptr);
        }
        draxul::shutdown_logging();
        return 1;
    }

    int status = 0;
#ifdef DRAXUL_ENABLE_RENDER_TESTS
    if (render_test)
    {
        auto frame = app.run_render_test(std::chrono::milliseconds(render_test->timeout_ms),
            std::chrono::milliseconds(render_test->settle_ms));
        if (!frame)
        {
            draxul::write_render_test_failure_report(*render_test, app.last_render_test_error());
            status = 1;
        }
        else
        {
            std::string finalize_error;
            bool ok = false;
            if (!parsed.export_render_test_path.empty())
                ok = draxul::export_render_test_frame(parsed.export_render_test_path, *frame, &finalize_error);
            else
                ok = draxul::finalize_render_test_result(*render_test, *frame, parsed.bless_render_test, &finalize_error);

            if (!ok)
            {
                DRAXUL_LOG_ERROR(draxul::LogCategory::Test, "Render test finalize failed: %s",
                    finalize_error.c_str());
                draxul::write_render_test_failure_report(*render_test, finalize_error);
                DRAXUL_LOG_ERROR(draxul::LogCategory::App, "%s", finalize_error.c_str());
                status = 1;
            }
        }
    }
    else if (parsed.smoke_test)
#else
    if (parsed.smoke_test)
#endif
    {
        if (!app.run_smoke_test(std::chrono::seconds(3)))
            status = 1;
    }
    else if (!parsed.screenshot_path.empty())
    {
        auto frame = app.run_screenshot(std::chrono::milliseconds(parsed.screenshot_delay_ms));
        if (frame)
        {
            if (!draxul::write_bmp_rgba(parsed.screenshot_path, *frame))
            {
                DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to write screenshot to %s",
                    parsed.screenshot_path.string().c_str());
                status = 1;
            }
        }
        else
        {
            DRAXUL_LOG_ERROR(draxul::LogCategory::App, "Failed to capture screenshot");
            status = 1;
        }
    }
    else
    {
        app.run();
    }
    app.shutdown();
    draxul::shutdown_logging();

    return status;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return draxul_main(command_line_args());
}
#else
int main(int argc, char* argv[])
{
    return draxul_main(command_line_args(argc, argv));
}
#endif
