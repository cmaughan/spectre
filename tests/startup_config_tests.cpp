// startup_config_tests.cpp — Verifies that App::initialize() failure does not
// persist config to disk (validates fix for 00-startup-config-save-on-failure).

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>
#include <draxul/app_config.h>
#include <fstream>

#include "app.h"

using namespace draxul;
using namespace draxul::tests;

namespace
{

AppOptions base_options()
{
    AppOptions opts;
    opts.load_user_config = false;
    opts.save_user_config = false; // callers set this explicitly
    opts.activate_window_on_startup = false;
    opts.clamp_window_to_display = false;
    return opts;
}

RendererBundle make_fake_renderer(int /*atlas_size*/, RendererOptions /*renderer_options*/)
{
    return RendererBundle{ std::make_unique<FakeTermRenderer>() };
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return {};
    return std::string(std::istreambuf_iterator<char>(f), {});
}

} // namespace

// ---------------------------------------------------------------------------
// Negative cases: failed init must never write to config
// ---------------------------------------------------------------------------

TEST_CASE("config not saved: window creation failure does not overwrite config [integration]",
    "[startup][config]")
{
    TempDir temp("draxul-cfg-no-save-window");
    HomeDirRedirect redir(temp.path);

    // Pre-populate a sentinel config file.
    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 1111\n";
    }
    const std::string before = read_file(redir.config_path);
    REQUIRE(!before.empty());

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("config file must be byte-for-byte identical after a failed window init");
    REQUIRE(read_file(redir.config_path) == before);
}

TEST_CASE("config not saved: renderer init failure does not overwrite config [integration]",
    "[startup][config]")
{
    TempDir temp("draxul-cfg-no-save-renderer");
    HomeDirRedirect redir(temp.path);

    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 2222\n";
    }
    const std::string before = read_file(redir.config_path);
    REQUIRE(!before.empty());

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = [](int, RendererOptions) { return RendererBundle{}; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("config file must be byte-for-byte identical after a renderer init failure");
    REQUIRE(read_file(redir.config_path) == before);
}

TEST_CASE("config not saved: font load failure does not overwrite config [integration]",
    "[startup][config]")
{
    TempDir temp("draxul-cfg-no-save-font");
    HomeDirRedirect redir(temp.path);

    std::filesystem::create_directories(redir.config_path.parent_path());
    {
        std::ofstream out(redir.config_path, std::ios::trunc);
        out << "window_width = 3333\n";
    }
    const std::string before = read_file(redir.config_path);
    REQUIRE(!before.empty());

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() { return std::make_unique<FakeWindow>(); };
    opts.renderer_create_fn = &make_fake_renderer;
    opts.config_overrides.font_path = "/nonexistent/draxul_test_fake_font.ttf";
    opts.override_display_ppi = 96.0f;

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("config file must be byte-for-byte identical after a font load failure");
    REQUIRE(read_file(redir.config_path) == before);
}

TEST_CASE("config not saved: failed init does not create config file when none exists [integration]",
    "[startup][config]")
{
    TempDir temp("draxul-cfg-no-create");
    HomeDirRedirect redir(temp.path);

    REQUIRE(!std::filesystem::exists(redir.config_path));

    AppOptions opts = base_options();
    opts.save_user_config = true;
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("config file must not be created after a failed init");
    REQUIRE(!std::filesystem::exists(redir.config_path));
}

// ---------------------------------------------------------------------------
// Positive case: config save happens when save_user_config is false (the
// production default for tests) — assert no side effects on the file system.
// ---------------------------------------------------------------------------

TEST_CASE("config not saved: save_user_config=false skips disk write on any failure [integration]",
    "[startup][config]")
{
    TempDir temp("draxul-cfg-no-save-flag");
    HomeDirRedirect redir(temp.path);

    REQUIRE(!std::filesystem::exists(redir.config_path));

    AppOptions opts = base_options();
    opts.save_user_config = false; // explicit
    opts.window_factory = []() -> std::unique_ptr<IWindow> { return nullptr; };

    App app(std::move(opts));
    REQUIRE(!app.initialize());

    INFO("save_user_config=false: no config file created");
    REQUIRE(!std::filesystem::exists(redir.config_path));
}
