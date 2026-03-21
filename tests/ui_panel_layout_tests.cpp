#include "support/test_support.h"

#include <draxul/ui_panel.h>

namespace
{

using namespace draxul;
using namespace draxul::tests;

void layout_hidden_uses_full_window_height()
{
    const PanelLayout layout = compute_panel_layout(1280, 800, 10, 20, 1, false);
    expect_eq(layout.visible, false, "panel should be hidden");
    expect_eq(layout.panel_height, 0, "hidden panel should not reserve height");
    expect_eq(layout.terminal_height, 800, "hidden panel should leave the full terminal height");
    expect_eq(layout.grid_cols, 127, "hidden panel should preserve full-width grid columns");
    expect_eq(layout.grid_rows, 39, "hidden panel should preserve full-height grid rows");
}

void layout_visible_reserves_bottom_panel_space()
{
    const PanelLayout layout = compute_panel_layout(1280, 800, 10, 20, 1, true);
    // desired = lround(800/3) = 267, grid_rows=26, snapped = 1 + 26*20 = 521
    expect_eq(layout.visible, true, "panel should be visible");
    expect_eq(layout.panel_height, 279, "panel height should fill remaining pixels after grid-snapped terminal");
    expect_eq(layout.panel_y, 521, "panel should start exactly at the bottom of the last terminal row");
    expect_eq(layout.terminal_height, 521, "terminal height should snap to where terminal content ends");
    expect_eq(layout.grid_cols, 127, "panel should not change grid columns");
    expect_eq(layout.grid_rows, 26, "panel should reduce the available grid rows");
    expect(layout.contains_panel_point(100, 600), "panel hit-testing should include bottom-region points");
    expect(!layout.contains_panel_point(100, 500), "panel hit-testing should exclude terminal points");
}

} // namespace

void run_ui_panel_layout_tests()
{
    run_test("ui panel layout hidden uses full window height", layout_hidden_uses_full_window_height);
    run_test("ui panel layout visible reserves bottom panel space", layout_visible_reserves_bottom_panel_space);
}
