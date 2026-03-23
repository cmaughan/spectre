
#include <catch2/catch_test_macros.hpp>

#include <draxul/highlight.h>

using namespace draxul;

TEST_CASE("highlight table resolves explicit colors and special color", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x101010));
    table.set_default_sp(color_from_rgb(0x00FF00));

    HlAttr attr;
    attr.fg = color_from_rgb(0x123456);
    attr.bg = color_from_rgb(0x654321);
    attr.sp = color_from_rgb(0xABCDEF);
    attr.has_fg = true;
    attr.has_bg = true;
    attr.has_sp = true;

    Color fg;
    Color bg;
    Color sp;
    table.resolve(attr, fg, bg, &sp);

    REQUIRE(fg == color_from_rgb(0x123456));
    REQUIRE(bg == color_from_rgb(0x654321));
    REQUIRE(sp == color_from_rgb(0xABCDEF));
}

TEST_CASE("highlight table reverses colors without losing accent color", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x101010));

    HlAttr attr;
    attr.fg = color_from_rgb(0x123456);
    attr.bg = color_from_rgb(0x654321);
    attr.has_fg = true;
    attr.has_bg = true;
    attr.reverse = true;

    Color fg;
    Color bg;
    Color sp;
    table.resolve(attr, fg, bg, &sp);

    REQUIRE(fg == color_from_rgb(0x654321));
    REQUIRE(bg == color_from_rgb(0x123456));
    REQUIRE(sp == fg);
}

TEST_CASE("highlight table: plain reverse-video swaps defaults", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x000000));

    HlAttr attr;
    attr.reverse = true;

    Color fg, bg;
    table.resolve(attr, fg, bg);

    INFO("reverse-video: default fg becomes bg");
    REQUIRE(fg == color_from_rgb(0x000000));
    INFO("reverse-video: default bg becomes fg");
    REQUIRE(bg == color_from_rgb(0xFFFFFF));
}

TEST_CASE("highlight table: reverse-video with explicit fg only", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x000000));

    HlAttr attr;
    attr.fg = color_from_rgb(0xFF0000);
    attr.has_fg = true;
    attr.reverse = true;

    Color fg, bg;
    table.resolve(attr, fg, bg);

    INFO("reverse-video + explicit fg: explicit fg becomes bg");
    REQUIRE(bg == color_from_rgb(0xFF0000));
    INFO("reverse-video + explicit fg: default bg becomes fg");
    REQUIRE(fg == color_from_rgb(0x000000));
}

TEST_CASE("highlight table: reverse-video with explicit bg only", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x000000));

    HlAttr attr;
    attr.bg = color_from_rgb(0x00FF00);
    attr.has_bg = true;
    attr.reverse = true;

    Color fg, bg;
    table.resolve(attr, fg, bg);

    INFO("reverse-video + explicit bg: explicit bg becomes fg");
    REQUIRE(fg == color_from_rgb(0x00FF00));
    INFO("reverse-video + explicit bg: default fg becomes bg");
    REQUIRE(bg == color_from_rgb(0xFFFFFF));
}

TEST_CASE("highlight table: reverse-video with both explicit fg and bg", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x000000));

    HlAttr attr;
    attr.fg = color_from_rgb(0xFF0000);
    attr.bg = color_from_rgb(0x00FF00);
    attr.has_fg = true;
    attr.has_bg = true;
    attr.reverse = true;

    Color fg, bg;
    table.resolve(attr, fg, bg);

    INFO("reverse-video + both explicit: fg and bg are swapped");
    REQUIRE(fg == color_from_rgb(0x00FF00));
    REQUIRE(bg == color_from_rgb(0xFF0000));
}

TEST_CASE("highlight table: no reverse with explicit colors passes through", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x000000));

    HlAttr attr;
    attr.fg = color_from_rgb(0xFF0000);
    attr.bg = color_from_rgb(0x00FF00);
    attr.has_fg = true;
    attr.has_bg = true;
    attr.reverse = false;

    Color fg, bg;
    table.resolve(attr, fg, bg);

    INFO("no reverse: fg passes through");
    REQUIRE(fg == color_from_rgb(0xFF0000));
    INFO("no reverse: bg passes through");
    REQUIRE(bg == color_from_rgb(0x00FF00));
}

TEST_CASE("highlight table: reverse-video special color falls back to post-swap fg", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x000000));

    HlAttr attr;
    attr.reverse = true;
    // No explicit sp — should fall back to fg (which after swap is 0x000000)

    Color fg, bg, sp;
    table.resolve(attr, fg, bg, &sp);

    INFO("special color falls back to post-swap fg");
    REQUIRE(sp == fg);
    REQUIRE(sp == color_from_rgb(0x000000));
}

TEST_CASE("highlight table: reverse-video with explicit special color preserves it", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xFFFFFF));
    table.set_default_bg(color_from_rgb(0x000000));

    HlAttr attr;
    attr.sp = color_from_rgb(0x0000FF);
    attr.has_sp = true;
    attr.reverse = true;

    Color fg, bg, sp;
    table.resolve(attr, fg, bg, &sp);

    INFO("explicit special color is not affected by reverse");
    REQUIRE(sp == color_from_rgb(0x0000FF));
}

TEST_CASE("highlight table: visual mode transition from normal to reverse", "[highlight]")
{
    HighlightTable table;
    table.set_default_fg(color_from_rgb(0xD4D4D4));
    table.set_default_bg(color_from_rgb(0x1E1E1E));

    // Normal mode highlight
    HlAttr normal;
    Color norm_fg, norm_bg;
    table.resolve(normal, norm_fg, norm_bg);

    // Visual mode highlight (reverse-video)
    HlAttr visual;
    visual.reverse = true;
    Color vis_fg, vis_bg;
    table.resolve(visual, vis_fg, vis_bg);

    INFO("normal mode uses default colors");
    REQUIRE(norm_fg == color_from_rgb(0xD4D4D4));
    REQUIRE(norm_bg == color_from_rgb(0x1E1E1E));
    INFO("visual mode swaps the colors");
    REQUIRE(vis_fg == color_from_rgb(0x1E1E1E));
    REQUIRE(vis_bg == color_from_rgb(0xD4D4D4));
}
