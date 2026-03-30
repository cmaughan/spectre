#include "support/test_support.h"

#include <draxul/app_config.h>
#include <draxul/log.h>

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

using namespace draxul;
using namespace draxul::tests;

// ---------------------------------------------------------------------------
// Config edge-case tests (work item #07)
// Supplements corrupt_config_recovery_tests.cpp with round-trip and
// additional load/save scenarios.
// ---------------------------------------------------------------------------

namespace
{

struct TempConfigDir
{
    std::filesystem::path dir;

    TempConfigDir()
    {
        static int counter = 0;
        dir = std::filesystem::temp_directory_path()
            / ("draxul-config-edge-" + std::to_string(++counter));
        std::filesystem::create_directories(dir);
    }

    ~TempConfigDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("config round-trip: save then load produces identical config", "[config][edge]")
{
    TempConfigDir tmp;
    auto path = tmp.dir / "config.toml";

    // Create a non-default config
    AppConfig original;
    original.font_size = 18.0f;
    original.enable_ligatures = false;
    original.smooth_scroll = false;
    original.scroll_speed = 2.5f;
    original.window_width = 1920;
    original.window_height = 1080;

    // Save
    original.save_to_path(path);
    REQUIRE(std::filesystem::exists(path));

    // Load
    AppConfig loaded = AppConfig::load_from_path(path);

    REQUIRE(loaded.font_size == original.font_size);
    REQUIRE(loaded.enable_ligatures == original.enable_ligatures);
    REQUIRE(loaded.smooth_scroll == original.smooth_scroll);
    REQUIRE(loaded.scroll_speed == original.scroll_speed);
    REQUIRE(loaded.window_width == original.window_width);
    REQUIRE(loaded.window_height == original.window_height);
}

TEST_CASE("config: load from non-existent path returns defaults", "[config][edge]")
{
    ScopedLogCapture cap;

    std::filesystem::path nonexistent("/tmp/does-not-exist-draxul-test/config.toml");
    auto loaded = AppConfig::load_from_path(nonexistent);
    AppConfig defaults;

    REQUIRE(loaded.font_size == defaults.font_size);
    REQUIRE(loaded.enable_ligatures == defaults.enable_ligatures);
    REQUIRE(loaded.smooth_scroll == defaults.smooth_scroll);
    REQUIRE(loaded.window_width == defaults.window_width);
}

TEST_CASE("config: parse empty string returns defaults", "[config][edge]")
{
    ScopedLogCapture cap;

    AppConfig loaded = AppConfig::parse("");
    AppConfig defaults;

    REQUIRE(loaded.font_size == defaults.font_size);
    REQUIRE(loaded.enable_ligatures == defaults.enable_ligatures);
}

TEST_CASE("config: known keys preserved alongside unknown keys", "[config][edge]")
{
    ScopedLogCapture cap(LogLevel::Warn);
    TempConfigDir tmp;
    auto path = tmp.dir / "config.toml";

    std::ofstream out(path);
    out << "font_size = 22.0\n"
        << "unknown_future_key = true\n"
        << "enable_ligatures = false\n";
    out.close();

    auto loaded = AppConfig::load_from_path(path);
    REQUIRE(loaded.font_size == 22.0f);
    REQUIRE(loaded.enable_ligatures == false);
}
