#include "support/test_support.h"

#include <draxul/highlight.h>
#include <draxul/types.h>

using namespace draxul;
using namespace draxul::tests;

// Work item 27 (hlattr-style-flags-fix) replaced the magic literals in
// HlAttr::style_flags() with named STYLE_FLAG_* constants from types.h.
// These tests verify that style_flags() returns the correct bit positions
// and that the named constants match the implementation.
//
// style_flags() mapping (from highlight.h, using STYLE_FLAG_* constants):
//   bold         -> STYLE_FLAG_BOLD          (1u << 0 = 1)
//   italic       -> STYLE_FLAG_ITALIC        (1u << 1 = 2)
//   underline    -> STYLE_FLAG_UNDERLINE     (1u << 2 = 4)
//   strikethrough-> STYLE_FLAG_STRIKETHROUGH (1u << 3 = 8)
//   undercurl    -> STYLE_FLAG_UNDERCURL     (1u << 4 = 16)

void run_hlattr_style_flags_tests()
{
    run_test("hlattr style_flags no flags set returns zero", []() {
        HlAttr attr;
        expect_eq(attr.style_flags(), static_cast<uint32_t>(0),
            "default HlAttr with no style flags should return 0");
    });

    run_test("hlattr style_flags bold only sets bold bit", []() {
        HlAttr attr;
        attr.bold = true;
        const uint32_t flags = attr.style_flags();
        expect((flags & STYLE_FLAG_BOLD) != 0, "bold flag should be set");
        expect((flags & STYLE_FLAG_ITALIC) == 0, "italic flag should be clear");
        expect((flags & STYLE_FLAG_UNDERLINE) == 0, "underline flag should be clear");
        expect((flags & STYLE_FLAG_STRIKETHROUGH) == 0, "strikethrough flag should be clear");
        expect((flags & STYLE_FLAG_UNDERCURL) == 0, "undercurl flag should be clear");
    });

    run_test("hlattr style_flags italic only sets italic bit", []() {
        HlAttr attr;
        attr.italic = true;
        const uint32_t flags = attr.style_flags();
        expect((flags & STYLE_FLAG_ITALIC) != 0, "italic flag should be set");
        expect((flags & STYLE_FLAG_BOLD) == 0, "bold flag should be clear");
        expect((flags & STYLE_FLAG_UNDERLINE) == 0, "underline flag should be clear");
        expect((flags & STYLE_FLAG_STRIKETHROUGH) == 0, "strikethrough flag should be clear");
        expect((flags & STYLE_FLAG_UNDERCURL) == 0, "undercurl flag should be clear");
    });

    run_test("hlattr style_flags underline only sets underline bit", []() {
        HlAttr attr;
        attr.underline = true;
        const uint32_t flags = attr.style_flags();
        expect((flags & STYLE_FLAG_UNDERLINE) != 0, "underline flag should be set");
        expect((flags & STYLE_FLAG_BOLD) == 0, "bold flag should be clear");
        expect((flags & STYLE_FLAG_ITALIC) == 0, "italic flag should be clear");
        expect((flags & STYLE_FLAG_STRIKETHROUGH) == 0, "strikethrough flag should be clear");
        expect((flags & STYLE_FLAG_UNDERCURL) == 0, "undercurl flag should be clear");
    });

    run_test("hlattr style_flags strikethrough only sets strikethrough bit", []() {
        HlAttr attr;
        attr.strikethrough = true;
        const uint32_t flags = attr.style_flags();
        expect((flags & STYLE_FLAG_STRIKETHROUGH) != 0, "strikethrough flag should be set");
        expect((flags & STYLE_FLAG_BOLD) == 0, "bold flag should be clear");
        expect((flags & STYLE_FLAG_ITALIC) == 0, "italic flag should be clear");
        expect((flags & STYLE_FLAG_UNDERLINE) == 0, "underline flag should be clear");
        expect((flags & STYLE_FLAG_UNDERCURL) == 0, "undercurl flag should be clear");
    });

    run_test("hlattr style_flags undercurl only sets undercurl bit", []() {
        HlAttr attr;
        attr.undercurl = true;
        const uint32_t flags = attr.style_flags();
        expect((flags & STYLE_FLAG_UNDERCURL) != 0, "undercurl flag should be set");
        expect((flags & STYLE_FLAG_BOLD) == 0, "bold flag should be clear");
        expect((flags & STYLE_FLAG_ITALIC) == 0, "italic flag should be clear");
        expect((flags & STYLE_FLAG_UNDERLINE) == 0, "underline flag should be clear");
        expect((flags & STYLE_FLAG_STRIKETHROUGH) == 0, "strikethrough flag should be clear");
    });

    run_test("hlattr style_flags all flags set returns all bits", []() {
        HlAttr attr;
        attr.bold = true;
        attr.italic = true;
        attr.underline = true;
        attr.strikethrough = true;
        attr.undercurl = true;

        const uint32_t flags = attr.style_flags();
        expect((flags & STYLE_FLAG_BOLD) != 0, "bold flag should be set");
        expect((flags & STYLE_FLAG_ITALIC) != 0, "italic flag should be set");
        expect((flags & STYLE_FLAG_UNDERLINE) != 0, "underline flag should be set");
        expect((flags & STYLE_FLAG_STRIKETHROUGH) != 0, "strikethrough flag should be set");
        expect((flags & STYLE_FLAG_UNDERCURL) != 0, "undercurl flag should be set");

        const uint32_t expected_all = STYLE_FLAG_BOLD | STYLE_FLAG_ITALIC | STYLE_FLAG_UNDERLINE
            | STYLE_FLAG_STRIKETHROUGH | STYLE_FLAG_UNDERCURL;
        expect_eq(flags, expected_all, "all five style flags should combine to the expected bitmask");
    });

    run_test("hlattr style flag constants are distinct powers of two", []() {
        // Each constant must be a non-zero power of two (exactly one bit set).
        auto is_power_of_two = [](uint32_t v) -> bool {
            return v != 0 && (v & (v - 1)) == 0;
        };

        expect(is_power_of_two(STYLE_FLAG_BOLD), "STYLE_FLAG_BOLD is a power of two");
        expect(is_power_of_two(STYLE_FLAG_ITALIC), "STYLE_FLAG_ITALIC is a power of two");
        expect(is_power_of_two(STYLE_FLAG_UNDERLINE), "STYLE_FLAG_UNDERLINE is a power of two");
        expect(is_power_of_two(STYLE_FLAG_STRIKETHROUGH), "STYLE_FLAG_STRIKETHROUGH is a power of two");
        expect(is_power_of_two(STYLE_FLAG_UNDERCURL), "STYLE_FLAG_UNDERCURL is a power of two");

        // No two constants share a bit.
        expect(STYLE_FLAG_BOLD != STYLE_FLAG_ITALIC, "BOLD and ITALIC are distinct");
        expect(STYLE_FLAG_BOLD != STYLE_FLAG_UNDERLINE, "BOLD and UNDERLINE are distinct");
        expect(STYLE_FLAG_BOLD != STYLE_FLAG_STRIKETHROUGH, "BOLD and STRIKETHROUGH are distinct");
        expect(STYLE_FLAG_BOLD != STYLE_FLAG_UNDERCURL, "BOLD and UNDERCURL are distinct");
        expect(STYLE_FLAG_ITALIC != STYLE_FLAG_UNDERLINE, "ITALIC and UNDERLINE are distinct");
        expect(STYLE_FLAG_ITALIC != STYLE_FLAG_STRIKETHROUGH, "ITALIC and STRIKETHROUGH are distinct");
        expect(STYLE_FLAG_ITALIC != STYLE_FLAG_UNDERCURL, "ITALIC and UNDERCURL are distinct");
        expect(STYLE_FLAG_UNDERLINE != STYLE_FLAG_STRIKETHROUGH, "UNDERLINE and STRIKETHROUGH are distinct");
        expect(STYLE_FLAG_UNDERLINE != STYLE_FLAG_UNDERCURL, "UNDERLINE and UNDERCURL are distinct");
        expect(STYLE_FLAG_STRIKETHROUGH != STYLE_FLAG_UNDERCURL, "STRIKETHROUGH and UNDERCURL are distinct");
    });

    run_test("hlattr style_flags bold constant matches implementation bit value", []() {
        HlAttr attr;
        attr.bold = true;
        expect_eq(attr.style_flags(), STYLE_FLAG_BOLD,
            "bold alone should equal STYLE_FLAG_BOLD (1)");
    });

    run_test("hlattr style_flags italic constant matches implementation bit value", []() {
        HlAttr attr;
        attr.italic = true;
        expect_eq(attr.style_flags(), STYLE_FLAG_ITALIC,
            "italic alone should equal STYLE_FLAG_ITALIC (2)");
    });

    run_test("hlattr style_flags underline constant matches implementation bit value", []() {
        HlAttr attr;
        attr.underline = true;
        expect_eq(attr.style_flags(), STYLE_FLAG_UNDERLINE,
            "underline alone should equal STYLE_FLAG_UNDERLINE (4)");
    });

    run_test("hlattr style_flags strikethrough constant matches implementation bit value", []() {
        HlAttr attr;
        attr.strikethrough = true;
        expect_eq(attr.style_flags(), STYLE_FLAG_STRIKETHROUGH,
            "strikethrough alone should equal STYLE_FLAG_STRIKETHROUGH (8)");
    });

    run_test("hlattr style_flags undercurl constant matches implementation bit value", []() {
        HlAttr attr;
        attr.undercurl = true;
        expect_eq(attr.style_flags(), STYLE_FLAG_UNDERCURL,
            "undercurl alone should equal STYLE_FLAG_UNDERCURL (16)");
    });
}
