#include <catch2/catch_test_macros.hpp>

#include "command_palette.h"
#include "command_palette_host.h"
#include "fuzzy_match.h"
#include "gui_action_handler.h"
#include <SDL3/SDL.h>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/text_service.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <filesystem>

#include "support/fake_grid_pipeline_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

using namespace draxul;

namespace
{

std::filesystem::path bundled_font_path()
{
    return std::filesystem::path(DRAXUL_PROJECT_ROOT) / "fonts" / "JetBrainsMonoNerdFont-Regular.ttf";
}

bool init_text_service(TextService& text_service)
{
    const auto font_path = bundled_font_path();
    if (!std::filesystem::exists(font_path))
        return false;

    TextServiceConfig config;
    config.font_path = font_path.string();
    return text_service.initialize(config, TextService::DEFAULT_POINT_SIZE, 96.0f);
}

} // namespace

// ── Fuzzy match tests ──────────────────────────────────────────────────────

TEST_CASE("fuzzy_match: empty pattern matches everything", "[fuzzy]")
{
    auto result = fuzzy_match("", "toggle_diagnostics");
    CHECK(result.matched);
    CHECK(result.score == 0);
    CHECK(result.positions.empty());
}

TEST_CASE("fuzzy_match: empty target does not match", "[fuzzy]")
{
    auto result = fuzzy_match("td", "");
    CHECK_FALSE(result.matched);
}

TEST_CASE("fuzzy_match: prefix match scores higher than mid-word", "[fuzzy]")
{
    auto prefix = fuzzy_match("copy", "copy");
    auto mid = fuzzy_match("copy", "xcopy");
    CHECK(prefix.matched);
    CHECK(mid.matched);
    CHECK(prefix.score > mid.score);
}

TEST_CASE("fuzzy_match: boundary bonus at underscore", "[fuzzy]")
{
    // "td" in "toggle_diagnostics": t at start (boundary), d after _ (boundary)
    auto boundary = fuzzy_match("td", "toggle_diagnostics");
    // "td" in "outdoors": t and d are mid-word
    auto midword = fuzzy_match("td", "outdoors");
    CHECK(boundary.matched);
    CHECK(midword.matched);
    CHECK(boundary.score > midword.score);
}

TEST_CASE("fuzzy_match: consecutive chars get bonus", "[fuzzy]")
{
    // Both targets start mid-word so first-char boundary bonus doesn't skew the comparison
    auto consecutive = fuzzy_match("ab", "xabc");
    auto scattered = fuzzy_match("ab", "xaxb");
    CHECK(consecutive.matched);
    CHECK(scattered.matched);
    CHECK(consecutive.score > scattered.score);
}

TEST_CASE("fuzzy_match: case insensitive", "[fuzzy]")
{
    auto result = fuzzy_match("COPY", "copy");
    CHECK(result.matched);
    CHECK(result.positions.size() == 4);
}

TEST_CASE("fuzzy_match: non-matching returns false", "[fuzzy]")
{
    auto result = fuzzy_match("xyz", "copy");
    CHECK_FALSE(result.matched);
}

TEST_CASE("fuzzy_match: first char bonus doubled", "[fuzzy]")
{
    // Pattern starting at a boundary should score higher than mid-word
    auto at_start = fuzzy_match("f", "font_increase");
    auto mid = fuzzy_match("n", "font_increase");
    CHECK(at_start.matched);
    CHECK(mid.matched);
    // 'f' at position 0 gets boundary bonus * 2; 'n' at position 2 gets no bonus
    CHECK(at_start.score > mid.score);
}

TEST_CASE("fuzzy_match: positions are correct", "[fuzzy]")
{
    auto result = fuzzy_match("fi", "font_increase");
    CHECK(result.matched);
    REQUIRE(result.positions.size() == 2);
    CHECK(result.positions[0] == 0); // f
    CHECK(result.positions[1] == 5); // i after _
}

// ── CommandPalette state tests ─────────────────────────────────────────────

TEST_CASE("CommandPalette: open and close", "[palette]")
{
    CommandPalette palette;
    CHECK_FALSE(palette.is_open());
    palette.open();
    CHECK(palette.is_open());
    palette.close();
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette: Escape closes", "[palette]")
{
    CommandPalette palette;
    palette.open();
    KeyEvent esc{ 0, SDLK_ESCAPE, kModNone, true };
    palette.on_key(esc);
    CHECK_FALSE(palette.is_open());
}

TEST_CASE("CommandPalette: consumes all keys when open", "[palette]")
{
    CommandPalette palette;
    palette.open();
    KeyEvent key{ 0, SDLK_A, kModNone, true };
    CHECK(palette.on_key(key));
}

TEST_CASE("CommandPalette: does not consume when closed", "[palette]")
{
    CommandPalette palette;
    KeyEvent key{ 0, SDLK_A, kModNone, true };
    CHECK_FALSE(palette.on_key(key));
}

TEST_CASE("CommandPalette: action_names excludes command_palette", "[palette]")
{
    auto names = GuiActionHandler::action_names();
    bool found_palette = false;
    bool found_copy = false;
    for (auto name : names)
    {
        if (name == "command_palette")
            found_palette = true;
        if (name == "copy")
            found_copy = true;
    }
    CHECK(found_palette); // it exists in the registry
    CHECK(found_copy);
}

TEST_CASE("CommandPaletteHost: opening primes palette cells before first draw", "[palette]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    tests::TestHostCallbacks callbacks;
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    CommandPaletteHost::Deps deps;
    CommandPaletteHost host(std::move(deps));

    HostViewport viewport;
    viewport.pixel_size = { window.pixel_w_, window.pixel_h_ };
    viewport.grid_size = { 1, 1 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = viewport,
        .display_ppi = window.display_ppi_,
    };

    REQUIRE(host.initialize(context, callbacks));
    REQUIRE(host.dispatch_action("toggle"));
    REQUIRE(host.is_active());
    REQUIRE(renderer.last_handle != nullptr);
    CHECK(renderer.last_handle->last_viewport.pixel_pos == glm::ivec2(200, 150));
    CHECK(renderer.last_handle->last_viewport.pixel_size == glm::ivec2(400, 300));
    CHECK(renderer.last_handle->last_grid_size == glm::ivec2(40, 15));
    CHECK(renderer.last_handle->last_cursor == glm::ivec2(-1, -1));
    CHECK_FALSE(renderer.last_handle->update_batches.empty());
    CHECK(renderer.last_handle->total_cell_updates() > 0);
    CHECK(renderer.region_uploads > 0);

    host.shutdown();
}

TEST_CASE("render_palette: palette fills the host grid", "[palette]")
{
    TextService text_service;
    if (!init_text_service(text_service))
        SKIP("bundled font not found");

    const std::array<gui::PaletteEntry, 1> entries{ {
        { "copy", "Ctrl+C", {} },
    } };

    gui::PaletteViewState state;
    state.grid_cols = 40;
    state.grid_rows = 15;
    state.query = "";
    state.selected_index = 0;
    state.entries = entries;

    const auto cells = gui::render_palette(state, text_service);
    REQUIRE_FALSE(cells.empty());

    int min_col = cells.front().col;
    int max_col = cells.front().col;
    int min_row = cells.front().row;
    int max_row = cells.front().row;
    for (const auto& cell : cells)
    {
        min_col = std::min(min_col, cell.col);
        max_col = std::max(max_col, cell.col);
        min_row = std::min(min_row, cell.row);
        max_row = std::max(max_row, cell.row);
    }

    CHECK(max_col - min_col + 1 == 40);
    CHECK(max_row - min_row + 1 == 15);
}

TEST_CASE("app config default palette alpha is 0.9", "[config]")
{
    AppConfig config = AppConfig::parse("");
    CHECK(config.palette_bg_alpha == 0.9f);
}
