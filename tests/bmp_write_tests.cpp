#include <catch2/catch_all.hpp>

#include <draxul/bmp.h>
#include <draxul/types.h>

#include <filesystem>
#include <system_error>

using namespace draxul;

namespace
{

CapturedFrame make_solid_frame(int width, int height)
{
    CapturedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.rgba.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);
    for (size_t i = 0; i < frame.rgba.size(); i += 4)
    {
        frame.rgba[i + 0] = 0x11;
        frame.rgba[i + 1] = 0x22;
        frame.rgba[i + 2] = 0x33;
        frame.rgba[i + 3] = 0xff;
    }
    return frame;
}

} // namespace

// Regression test for WI 10 / WI 106: write_bmp_rgba must not throw when the
// caller passes a bare filename with no directory component. Prior to the
// fix, std::filesystem::create_directories("") threw filesystem_error on
// macOS and Windows, terminating the process from --screenshot out.bmp.
TEST_CASE("write_bmp_rgba accepts a bare filename without throwing", "[bmp]")
{
    namespace fs = std::filesystem;

    const auto original_cwd = fs::current_path();
    std::error_code ec;
    const auto sandbox = fs::temp_directory_path(ec) / "draxul-bmp-bare-filename-test";
    REQUIRE_FALSE(ec);

    fs::remove_all(sandbox, ec);
    fs::create_directories(sandbox, ec);
    REQUIRE_FALSE(ec);

    fs::current_path(sandbox, ec);
    REQUIRE_FALSE(ec);

    const auto frame = make_solid_frame(4, 3);
    const fs::path bare_path("bare_filename.bmp");
    REQUIRE(bare_path.parent_path().empty());

    bool wrote = false;
    REQUIRE_NOTHROW(wrote = write_bmp_rgba(bare_path, frame));
    REQUIRE(wrote);
    REQUIRE(fs::exists(bare_path));

    auto round_trip = read_bmp_rgba(bare_path);
    REQUIRE(round_trip.has_value());
    REQUIRE(round_trip->width == frame.width);
    REQUIRE(round_trip->height == frame.height);
    REQUIRE(round_trip->rgba == frame.rgba);

    fs::current_path(original_cwd, ec);
    fs::remove_all(sandbox, ec);
}

TEST_CASE("write_bmp_rgba creates missing parent directories", "[bmp]")
{
    namespace fs = std::filesystem;

    std::error_code ec;
    const auto sandbox = fs::temp_directory_path(ec) / "draxul-bmp-parent-test";
    REQUIRE_FALSE(ec);

    fs::remove_all(sandbox, ec);

    const auto nested = sandbox / "a" / "b" / "c" / "nested.bmp";
    const auto frame = make_solid_frame(2, 2);

    bool wrote = false;
    REQUIRE_NOTHROW(wrote = write_bmp_rgba(nested, frame));
    REQUIRE(wrote);
    REQUIRE(fs::exists(nested));

    fs::remove_all(sandbox, ec);
}
