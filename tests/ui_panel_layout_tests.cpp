
#include <draxul/ui_panel.h>

#include <catch2/catch_all.hpp>

namespace
{

using namespace draxul;

void layout_hidden_uses_full_window_height()
{
    const PanelLayout layout = compute_panel_layout(1280, 800, 10, 20, 1, false);
    INFO("panel should be hidden");
    REQUIRE(layout.visible == false);
    INFO("hidden panel should not reserve height");
    REQUIRE(layout.panel_height == 0);
    INFO("hidden panel should leave the full terminal height");
    REQUIRE(layout.terminal_height == 800);
    INFO("hidden panel should preserve full-width grid columns");
    REQUIRE(layout.grid_size.x == 127);
    INFO("hidden panel should preserve full-height grid rows");
    REQUIRE(layout.grid_size.y == 39);
}

void layout_visible_reserves_bottom_panel_space()
{
    const PanelLayout layout = compute_panel_layout(1280, 800, 10, 20, 1, true);
    // desired = lround(800/3) = 267, grid_rows=26, snapped = 1 + 26*20 = 521
    INFO("panel should be visible");
    REQUIRE(layout.visible == true);
    INFO("panel height should fill remaining pixels after grid-snapped terminal");
    REQUIRE(layout.panel_height == 279);
    INFO("panel should start exactly at the bottom of the last terminal row");
    REQUIRE(layout.panel_y == 521);
    INFO("terminal height should snap to where terminal content ends");
    REQUIRE(layout.terminal_height == 521);
    INFO("panel should not change grid columns");
    REQUIRE(layout.grid_size.x == 127);
    INFO("panel should reduce the available grid rows");
    REQUIRE(layout.grid_size.y == 26);
    INFO("panel hit-testing should include bottom-region points");
    REQUIRE(layout.contains_panel_point(100, 600));
    INFO("panel hit-testing should exclude terminal points");
    REQUIRE(!layout.contains_panel_point(100, 500));
}

void hittest_boundaries_1x_scale()
{
    const PanelLayout layout = compute_panel_layout(1280, 800, 10, 20, 1, true, 1.0f);
    REQUIRE(layout.pixel_scale == 1.0f);
    REQUIRE(layout.panel_y == 521);
    REQUIRE(layout.panel_height == 279);

    INFO("top edge of panel is inclusive");
    REQUIRE(layout.contains_panel_point(0, layout.panel_y));
    INFO("one pixel above the panel is excluded");
    REQUIRE(!layout.contains_panel_point(0, layout.panel_y - 1));
    INFO("bottom-right inside corner is included");
    REQUIRE(layout.contains_panel_point(layout.window_size.x - 1, layout.panel_y + layout.panel_height - 1));
    INFO("right edge is exclusive (window_size.x is past the last column)");
    REQUIRE(!layout.contains_panel_point(layout.window_size.x, layout.panel_y));
    INFO("bottom edge is exclusive (panel_y + panel_height is past the last row)");
    REQUIRE(!layout.contains_panel_point(0, layout.panel_y + layout.panel_height));
    INFO("negative x is excluded");
    REQUIRE(!layout.contains_panel_point(-1, layout.panel_y));
}

void hittest_boundaries_2x_scale()
{
    // Caller passes physical pixels: a 1280x800-point window on a 2x display becomes 2560x1600
    // physical pixels with 20x40-pixel cells. WI 49 regression guard: hit-tests must use these
    // physical coordinates, not point-space coordinates.
    const PanelLayout layout = compute_panel_layout(2560, 1600, 20, 40, 2, true, 2.0f);
    REQUIRE(layout.pixel_scale == 2.0f);
    REQUIRE(layout.window_size.x == 2560);
    REQUIRE(layout.window_size.y == 1600);

    INFO("panel must extend to the bottom of the physical window");
    REQUIRE(layout.panel_y + layout.panel_height == 1600);
    INFO("panel must occupy a meaningful portion of the physical window");
    REQUIRE(layout.panel_height >= 120);

    const int top = layout.panel_y;
    const int bottom_inside = top + layout.panel_height - 1;
    INFO("top edge is inclusive at physical coordinates");
    REQUIRE(layout.contains_panel_point(0, top));
    INFO("one physical pixel above the panel is excluded");
    REQUIRE(!layout.contains_panel_point(0, top - 1));
    INFO("bottom-right inside corner at physical coordinates is included");
    REQUIRE(layout.contains_panel_point(2559, bottom_inside));
    INFO("right physical edge is exclusive");
    REQUIRE(!layout.contains_panel_point(2560, top));
    INFO("regression guard: a point that would be inside if treated as logical 1x coordinates "
         "must NOT be misread as inside the panel — physical coords win");
    // The midpoint of the *logical* terminal area (logical y ~= 260, physical y ~= 520) is
    // well above the panel; it must not be classified as a panel hit even though the
    // panel does sit at logical y >= ~520.
    REQUIRE(!layout.contains_panel_point(200, 400));
}

void hidden_panel_never_matches()
{
    const PanelLayout layout_1x = compute_panel_layout(1280, 800, 10, 20, 1, false, 1.0f);
    const PanelLayout layout_2x = compute_panel_layout(2560, 1600, 20, 40, 2, false, 2.0f);
    INFO("hidden 1x panel rejects all points");
    REQUIRE(!layout_1x.contains_panel_point(100, 600));
    REQUIRE(!layout_1x.contains_panel_point(0, 0));
    INFO("hidden 2x panel rejects all points");
    REQUIRE(!layout_2x.contains_panel_point(200, 1200));
    REQUIRE(!layout_2x.contains_panel_point(0, 0));
}

} // namespace

TEST_CASE("ui panel layout hidden uses full window height", "[ui]")
{
    layout_hidden_uses_full_window_height();
}

TEST_CASE("ui panel layout visible reserves bottom panel space", "[ui]")
{
    layout_visible_reserves_bottom_panel_space();
}

TEST_CASE("ui panel hit-test boundaries at 1x scale", "[ui]")
{
    hittest_boundaries_1x_scale();
}

TEST_CASE("ui panel hit-test boundaries at 2x scale", "[ui]")
{
    hittest_boundaries_2x_scale();
}

TEST_CASE("ui panel hit-test rejects everything when hidden", "[ui]")
{
    hidden_panel_never_matches();
}
