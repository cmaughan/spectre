// Regression guard for WI 26 (cli-numeric-arg-crash). Ensures the CLI parser
// rejects malformed numeric flags with a useful error rather than crashing
// or silently accepting them, and confirms that valid numeric flags parse.

#include <catch2/catch_test_macros.hpp>

#include "cli_args.h"

#include <string>
#include <vector>

using namespace draxul;

namespace
{

ParseArgsResult parse(std::initializer_list<const char*> tokens)
{
    std::vector<std::string> args;
    args.emplace_back("draxul"); // argv[0]
    for (const char* t : tokens)
        args.emplace_back(t);
    return parse_args(args);
}

} // namespace

TEST_CASE("cli: --screenshot-delay with non-numeric value reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "abc" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-delay") != std::string::npos);
}

TEST_CASE("cli: --screenshot-delay with negative value reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "-1" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-delay") != std::string::npos);
}

TEST_CASE("cli: --screenshot-delay with trailing garbage reports an error", "[cli]")
{
    // Without strict end-of-input parsing, "100x" would be silently truncated.
    auto r = parse({ "--screenshot-delay", "100x" });
    REQUIRE(r.error.has_value());
}

TEST_CASE("cli: --screenshot-delay with valid value parses cleanly", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "100" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_delay_ms == 100);
}

TEST_CASE("cli: --screenshot-delay zero is accepted (non-negative)", "[cli]")
{
    auto r = parse({ "--screenshot-delay", "0" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_delay_ms == 0);
}

TEST_CASE("cli: --screenshot-size with zero dimension reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-size", "0x600" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-size") != std::string::npos);
}

TEST_CASE("cli: --screenshot-size with non-numeric dimensions reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-size", "xyz" });
    REQUIRE(r.error.has_value());
    REQUIRE(r.error->find("--screenshot-size") != std::string::npos);
}

TEST_CASE("cli: --screenshot-size with garbled width reports an error", "[cli]")
{
    auto r = parse({ "--screenshot-size", "abcx600" });
    REQUIRE(r.error.has_value());
}

TEST_CASE("cli: --screenshot-size with valid value parses both dimensions", "[cli]")
{
    auto r = parse({ "--screenshot-size", "1024x768" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_width == 1024);
    REQUIRE(r.args.screenshot_height == 768);
}

TEST_CASE("cli: no args produces default ParsedArgs without error", "[cli]")
{
    auto r = parse({});
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.screenshot_delay_ms == 6000);
    REQUIRE(r.args.screenshot_width == 0);
    REQUIRE(r.args.screenshot_height == 0);
    REQUIRE_FALSE(r.args.smoke_test);
}

TEST_CASE("cli: --smoke-test sets the flag", "[cli]")
{
    auto r = parse({ "--smoke-test" });
    REQUIRE_FALSE(r.error.has_value());
    REQUIRE(r.args.smoke_test);
}
