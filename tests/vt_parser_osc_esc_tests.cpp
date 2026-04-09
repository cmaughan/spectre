#include <catch2/catch_test_macros.hpp>

#include <draxul/vt_parser.h>

#include <string>
#include <string_view>
#include <vector>

using namespace draxul;

namespace
{

struct TraceHarness
{
    std::vector<std::string> events;
    VtParser parser;

    TraceHarness()
        : parser([this]() {
            VtParser::Callbacks cbs;
            cbs.on_cluster = [this](const std::string& cluster) {
                events.push_back("cluster:" + cluster);
            };
            cbs.on_control = [this](char ch) {
                char buf[8];
                snprintf(buf, sizeof(buf), "0x%02X", static_cast<unsigned char>(ch));
                events.push_back("control:" + std::string(buf));
            };
            cbs.on_csi = [this](char final_ch, std::string_view body) {
                events.push_back("csi:" + std::string(body) + "|" + std::string(1, final_ch));
            };
            cbs.on_osc = [this](std::string_view body) {
                events.push_back("osc:" + std::string(body));
            };
            cbs.on_esc = [this](char ch) {
                events.push_back("esc:" + std::string(1, ch));
            };
            return cbs;
        }())
    {
    }
};

} // namespace

TEST_CASE("OscEsc: ESC \\ terminates OSC normally", "[vt_parser][osc_esc]")
{
    TraceHarness h;
    // OSC 0;title ESC backslash  (proper ST terminator)
    h.parser.feed("\x1B]0;title\x1B\\");
    REQUIRE(h.events.size() == 1);
    CHECK(h.events[0] == "osc:0;title");
}

TEST_CASE("OscEsc: ESC [ re-dispatches as CSI start", "[vt_parser][osc_esc]")
{
    TraceHarness h;
    // OSC 0;title ESC [ A  —  the ESC implicitly ends the OSC, then [A is a CSI
    h.parser.feed("\x1B]0;title\x1B[A");
    REQUIRE(h.events.size() == 2);
    CHECK(h.events[0] == "osc:0;title");
    CHECK(h.events[1] == "csi:|A");
}

TEST_CASE("OscEsc: ESC ] re-dispatches as new OSC start", "[vt_parser][osc_esc]")
{
    TraceHarness h;
    // OSC 0;first ESC ] 0;second BEL
    h.parser.feed("\x1B]0;first\x1B]0;second\x07");
    REQUIRE(h.events.size() == 2);
    CHECK(h.events[0] == "osc:0;first");
    CHECK(h.events[1] == "osc:0;second");
}

TEST_CASE("OscEsc: ESC + unknown char re-dispatches via on_esc", "[vt_parser][osc_esc]")
{
    TraceHarness h;
    // OSC 0;title ESC 7  — ESC implicitly ends the OSC, then ESC 7 = DECSC
    h.parser.feed("\x1B]0;title\x1B"
                  "7");
    REQUIRE(h.events.size() == 2);
    CHECK(h.events[0] == "osc:0;title");
    CHECK(h.events[1] == "esc:7");
}

TEST_CASE("OscEsc: CSI with params after implicit OSC termination", "[vt_parser][osc_esc]")
{
    TraceHarness h;
    // OSC 7;/home ESC [ 1 ; 3 1 m  — set title then SGR
    h.parser.feed("\x1B]7;/home\x1B[1;31m");
    REQUIRE(h.events.size() == 2);
    CHECK(h.events[0] == "osc:7;/home");
    CHECK(h.events[1] == "csi:1;31|m");
}

TEST_CASE("OscEsc: split feed across ESC boundary still works", "[vt_parser][osc_esc]")
{
    TraceHarness h;
    // Feed in two parts: first part ends with the ESC, second part has [A
    h.parser.feed("\x1B]0;title\x1B");
    h.parser.feed("[A");
    REQUIRE(h.events.size() == 2);
    CHECK(h.events[0] == "osc:0;title");
    CHECK(h.events[1] == "csi:|A");
}
