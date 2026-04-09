#include <catch2/catch_all.hpp>

#include <draxul/bmp.h>
#include <draxul/types.h>

#include <filesystem>
#include <fstream>
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

// Regression test for WI 06: read_bmp_rgba must reject a BMP whose height
// field is INT32_MIN, because std::abs(INT32_MIN) is undefined behaviour.
TEST_CASE("read_bmp_rgba rejects height == INT32_MIN", "[bmp]")
{
    namespace fs = std::filesystem;

    // Build a minimal 54-byte BMP header with height = INT32_MIN (0x80000000).
    // The pixel data is irrelevant because the header validation should reject
    // the file before reaching pixel reads.
    std::vector<uint8_t> header(54, 0);

    // BMP magic
    header[0] = 0x42;
    header[1] = 0x4D; // 'BM'

    // File size (header only, 54 bytes)
    header[2] = 54;

    // Pixel data offset
    header[10] = 54;

    // DIB header size
    header[14] = 40;

    // Width = 1
    header[18] = 1;

    // Height = INT32_MIN = 0x80000000
    header[22] = 0x00;
    header[23] = 0x00;
    header[24] = 0x00;
    header[25] = 0x80;

    // Planes = 1
    header[26] = 1;

    // BPP = 32
    header[28] = 32;

    // Compression = 0 (already zero)

    std::error_code ec;
    const auto sandbox = fs::temp_directory_path(ec) / "draxul-bmp-int32min-test";
    REQUIRE_FALSE(ec);
    fs::remove_all(sandbox, ec);
    fs::create_directories(sandbox, ec);
    REQUIRE_FALSE(ec);

    const auto bmp_path = sandbox / "int32min.bmp";
    {
        std::ofstream out(bmp_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(header.data()),
            static_cast<std::streamsize>(header.size()));
        REQUIRE(out.good());
    }

    auto result = draxul::read_bmp_rgba(bmp_path);
    REQUIRE_FALSE(result.has_value());

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
