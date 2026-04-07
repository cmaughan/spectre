#include <catch2/catch_test_macros.hpp>

#include <draxul/runtime_path.h>

TEST_CASE("runtime path: executable directory is absolute", "[runtime_path]")
{
    const auto exe_dir = draxul::executable_directory();
    REQUIRE_FALSE(exe_dir.empty());
    REQUIRE(exe_dir.is_absolute());
}

TEST_CASE("runtime path: bundled assets resolve relative to the executable", "[runtime_path]")
{
    const auto exe_dir = draxul::executable_directory();
    REQUIRE_FALSE(exe_dir.empty());

    const auto relative = std::filesystem::path("shaders") / "grid_bg.vert.spv";
    REQUIRE(draxul::bundled_asset_path(relative).lexically_normal() == (exe_dir / relative).lexically_normal());
}

TEST_CASE("runtime path: absolute asset paths are preserved", "[runtime_path]")
{
    const auto absolute = (std::filesystem::temp_directory_path() / "draxul-runtime-path-test.bin").lexically_normal();
    REQUIRE(draxul::bundled_asset_path(absolute) == absolute);
}
