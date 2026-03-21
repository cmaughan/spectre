#include "support/test_support.h"

#include <draxul/text_service.h>
#include <draxul/ui_panel.h>
#include <filesystem>

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::filesystem::path bundled_font_path()
{
    auto here = std::filesystem::path(__FILE__).parent_path();
    return here.parent_path() / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

// Mirrors SdlWindow::display_ppi(): ppi = 96 * display_scale
float compute_display_ppi(float display_scale)
{
    return 96.0f * display_scale;
}

} // namespace

void run_dpi_scaling_tests()
{
    // --- Display PPI formula ---

    run_test("dpi 1.0x scale produces 96 ppi", []() {
        const float ppi = compute_display_ppi(1.0f);
        expect_eq(ppi, 96.0f, "1x scale should produce 96 ppi");
    });

    run_test("dpi 2.0x scale produces 192 ppi", []() {
        const float ppi = compute_display_ppi(2.0f);
        expect_eq(ppi, 192.0f, "2x scale should produce 192 ppi");
    });

    run_test("dpi 1.5x scale produces 144 ppi", []() {
        const float ppi = compute_display_ppi(1.5f);
        expect_eq(ppi, 144.0f, "1.5x scale should produce 144 ppi");
    });

    // --- Grid dimensions via compute_panel_layout ---
    // compute_panel_layout(pixel_w, pixel_h, cell_w, cell_h, padding, visible)
    // grid_cols = (pixel_w - 2*padding) / cell_w
    // grid_rows = (pixel_h - 2*padding) / cell_h

    run_test("dpi 1.0x: grid dimensions match window pixels divided by cell size", []() {
        // 1x: 800x600 window, 8x16 cells, 1px padding
        const PanelLayout layout = compute_panel_layout(800, 600, 8, 16, 1, false);
        // cols = (800 - 2) / 8 = 99
        // rows = (600 - 2) / 16 = 37
        expect_eq(layout.grid_cols, 99, "1x scale cols match pixel division");
        expect_eq(layout.grid_rows, 37, "1x scale rows match pixel division");
    });

    run_test("dpi 2.0x: same logical window with doubled cell size halves grid dimensions", []() {
        // 2x Retina: window is 1600x1200 pixels but cells are 16x32 (doubled)
        // This simulates the same logical 800x600 window at 2x pixel density
        const PanelLayout layout_1x = compute_panel_layout(800, 600, 8, 16, 1, false);
        const PanelLayout layout_2x = compute_panel_layout(1600, 1200, 16, 32, 1, false);
        // Both should yield approximately the same grid dimensions
        // 1x: cols=(800-2)/8=99, rows=(600-2)/16=37
        // 2x: cols=(1600-2)/16=99, rows=(1200-2)/32=37
        expect_eq(layout_1x.grid_cols, layout_2x.grid_cols,
            "2x retina: same logical grid cols when pixel size and cell size both double");
        expect_eq(layout_1x.grid_rows, layout_2x.grid_rows,
            "2x retina: same logical grid rows when pixel size and cell size both double");
    });

    run_test("dpi 1.5x fractional scale: grid dimensions round down correctly", []() {
        // 1.5x: window 1200x900 pixels, cells 12x24 (1.5x of 8x16)
        // cols = (1200 - 2) / 12 = 99 (1198/12 = 99.8... → 99)
        // rows = (900 - 2) / 24 = 37 (898/24 = 37.4... → 37)
        const PanelLayout layout = compute_panel_layout(1200, 900, 12, 24, 1, false);
        expect_eq(layout.grid_cols, 99, "1.5x scale cols round down correctly");
        expect_eq(layout.grid_rows, 37, "1.5x scale rows round down correctly");
    });

    run_test("dpi window resize with constant dpi recalculates grid dimensions", []() {
        // Same DPI (1x), window grows from 800x600 to 1000x700
        const PanelLayout small = compute_panel_layout(800, 600, 8, 16, 1, false);
        const PanelLayout large = compute_panel_layout(1000, 700, 8, 16, 1, false);
        // small: cols=(800-2)/8=99, rows=(600-2)/16=37
        // large: cols=(1000-2)/8=124, rows=(700-2)/16=43
        expect(large.grid_cols > small.grid_cols, "wider window gives more columns at same DPI");
        expect(large.grid_rows > small.grid_rows, "taller window gives more rows at same DPI");
        expect_eq(large.grid_cols, 124, "wider window columns at 1x DPI");
        expect_eq(large.grid_rows, 43, "taller window rows at 1x DPI");
    });

    // --- TextService: cell metrics scale with PPI ---

    run_test("dpi 2.0x ppi produces larger cell dimensions than 1.0x ppi", []() {
        auto font_path = bundled_font_path();
        if (!std::filesystem::exists(font_path))
            skip("bundled font not available");

        TextService svc_1x, svc_2x;
        TextServiceConfig cfg;
        cfg.font_path = font_path.string();

        expect(svc_1x.initialize(cfg, 11, compute_display_ppi(1.0f)), "1x text service initializes");
        expect(svc_2x.initialize(cfg, 11, compute_display_ppi(2.0f)), "2x text service initializes");

        const auto& m1 = svc_1x.metrics();
        const auto& m2 = svc_2x.metrics();

        expect(m2.cell_width > m1.cell_width, "2x ppi produces wider cells than 1x ppi");
        expect(m2.cell_height > m1.cell_height, "2x ppi produces taller cells than 1x ppi");

        svc_1x.shutdown();
        svc_2x.shutdown();
    });

    run_test("dpi 1.5x ppi produces cell dimensions between 1.0x and 2.0x", []() {
        auto font_path = bundled_font_path();
        if (!std::filesystem::exists(font_path))
            skip("bundled font not available");

        TextService svc_1x, svc_15x, svc_2x;
        TextServiceConfig cfg;
        cfg.font_path = font_path.string();

        expect(svc_1x.initialize(cfg, 11, compute_display_ppi(1.0f)), "1x text service initializes");
        expect(svc_15x.initialize(cfg, 11, compute_display_ppi(1.5f)), "1.5x text service initializes");
        expect(svc_2x.initialize(cfg, 11, compute_display_ppi(2.0f)), "2x text service initializes");

        const auto& m1 = svc_1x.metrics();
        const auto& m15 = svc_15x.metrics();
        const auto& m2 = svc_2x.metrics();

        expect(m15.cell_height >= m1.cell_height,
            "1.5x ppi cell height is not smaller than 1x");
        expect(m15.cell_height <= m2.cell_height,
            "1.5x ppi cell height is not larger than 2x");

        svc_1x.shutdown();
        svc_15x.shutdown();
        svc_2x.shutdown();
    });

    run_test("dpi cell size at 1.0x feeds correct grid dimensions to panel layout", []() {
        auto font_path = bundled_font_path();
        if (!std::filesystem::exists(font_path))
            skip("bundled font not available");

        TextService svc;
        TextServiceConfig cfg;
        cfg.font_path = font_path.string();
        expect(svc.initialize(cfg, 11, compute_display_ppi(1.0f)), "text service initializes");

        const auto& m = svc.metrics();
        expect(m.cell_width > 0, "cell width is positive");
        expect(m.cell_height > 0, "cell height is positive");

        // A typical 1x HD window: 1280x800
        const PanelLayout layout = compute_panel_layout(1280, 800, m.cell_width, m.cell_height, 1, false);
        expect(layout.grid_cols > 0, "grid has at least one column");
        expect(layout.grid_rows > 0, "grid has at least one row");
        // Columns should fit roughly 1280/cell_width characters
        const int expected_cols = (1280 - 2) / m.cell_width;
        const int expected_rows = (800 - 2) / m.cell_height;
        expect_eq(layout.grid_cols, expected_cols, "grid cols match expected cell division");
        expect_eq(layout.grid_rows, expected_rows, "grid rows match expected cell division");

        svc.shutdown();
    });

    run_test("dpi hotplug stress: 20 rapid scale changes produce consistent final state", []() {
        auto font_path = bundled_font_path();
        if (!std::filesystem::exists(font_path))
            skip("bundled font not available");

        // Simulate 20 rapid DPI changes by reinitializing with alternating scales.
        // The terminal must produce consistent state after each change.
        const std::vector<float> scales = {
            1.0f, 2.0f, 1.5f, 1.0f, 2.0f, 1.25f, 1.75f, 1.0f, 2.0f, 1.5f,
            1.0f, 1.5f, 2.0f, 1.0f, 1.25f, 1.75f, 2.0f, 1.5f, 1.0f, 2.0f
        };

        TextService svc;
        TextServiceConfig cfg;
        cfg.font_path = font_path.string();

        for (size_t i = 0; i < scales.size(); ++i)
        {
            // Reinitialise for each "hotplug" event
            if (i > 0)
                svc.shutdown();
            expect(svc.initialize(cfg, 11, compute_display_ppi(scales[i])),
                "text service reinitialises after scale change");
            const auto& m = svc.metrics();
            expect(m.cell_width > 0, "cell width positive after scale change");
            expect(m.cell_height > 0, "cell height positive after scale change");
        }

        // Final state should match the last scale (2.0x)
        const float expected_ppi = compute_display_ppi(2.0f);
        TextService reference;
        expect(reference.initialize(cfg, 11, expected_ppi), "reference service initializes at 2x");
        expect_eq(svc.metrics().cell_width, reference.metrics().cell_width,
            "final state cell_width matches fresh 2x init");
        expect_eq(svc.metrics().cell_height, reference.metrics().cell_height,
            "final state cell_height matches fresh 2x init");

        svc.shutdown();
        reference.shutdown();
    });

    run_test("dpi hotplug stress: panel layout consistent after each scale change", []() {
        // Simulate 20 rapid DPI changes via compute_panel_layout calls
        // (pure geometry, no font init needed)
        const std::vector<float> scales = {
            1.0f, 2.0f, 1.5f, 1.0f, 2.0f, 1.25f, 1.75f, 1.0f, 2.0f, 1.5f,
            1.0f, 1.5f, 2.0f, 1.0f, 1.25f, 1.75f, 2.0f, 1.5f, 1.0f, 2.0f
        };

        int last_cols = -1;
        int last_rows = -1;
        for (size_t i = 0; i < scales.size(); ++i)
        {
            const float scale = scales[i];
            const int px_w = static_cast<int>(1280 * scale);
            const int px_h = static_cast<int>(800 * scale);
            const int cell_w = static_cast<int>(8 * scale);
            const int cell_h = static_cast<int>(16 * scale);
            const PanelLayout layout = compute_panel_layout(px_w, px_h, cell_w, cell_h, 1, false);
            expect(layout.grid_cols > 0, "grid_cols positive after rapid scale change");
            expect(layout.grid_rows > 0, "grid_rows positive after rapid scale change");
            last_cols = layout.grid_cols;
            last_rows = layout.grid_rows;
        }
        // Final state is 2x scale; cols/rows should match a fresh 2x layout
        const PanelLayout final_layout = compute_panel_layout(1280 * 2, 800 * 2, 8 * 2, 16 * 2, 1, false);
        expect_eq(last_cols, final_layout.grid_cols, "final cols after 20 hotplug events matches fresh 2x layout");
        expect_eq(last_rows, final_layout.grid_rows, "final rows after 20 hotplug events matches fresh 2x layout");
    });
}
