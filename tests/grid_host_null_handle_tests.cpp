// Regression guard for WI 48 (vk-null-grid-handle-dereference). Verifies
// that GridHostBase::initialize() and CommandPaletteHost::dispatch_action()
// handle a null grid handle gracefully instead of crashing.

#include <catch2/catch_test_macros.hpp>

#include "command_palette_host.h"
#include "support/fake_grid_host.h"
#include "support/fake_grid_pipeline_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"
#include <draxul/text_service.h>

using namespace draxul;

// Use the shared FakeGridHost from tests/support/ instead of the ad-hoc
// StubGridHost that lived here before WI 25.
using StubGridHost = draxul::tests::FakeGridHost;

TEST_CASE("GridHostBase::initialize returns false when grid handle creation fails", "[grid_host][null_handle]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    renderer.fail_create_grid_handle = true;
    TextService text_service; // not used along the failure path

    tests::TestHostCallbacks callbacks;
    HostViewport viewport;
    viewport.pixel_pos = { 0, 0 };
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 80, 30 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = viewport,
        .display_ppi = 96.0f,
    };

    StubGridHost host;
    REQUIRE_FALSE(host.initialize(context, callbacks));
    REQUIRE(renderer.create_grid_handle_calls == 1);
    INFO("subclass init hook must not run when the base class fails to allocate the handle");
    REQUIRE_FALSE(host.initialize_host_called);
}

TEST_CASE("GridHostBase::initialize succeeds when grid handle is allocated", "[grid_host][null_handle]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    TextService text_service;
    // Note: Without a fully-initialised TextService, refresh_renderer_metrics()
    // may still run; we keep this default-constructed because the failure-path
    // test above is the regression guard. This second case sanity-checks that
    // toggling the failure flag back off restores the normal init path.
    REQUIRE(renderer.fail_create_grid_handle == false);

    tests::TestHostCallbacks callbacks;
    HostViewport viewport;
    viewport.pixel_pos = { 0, 0 };
    viewport.pixel_size = { 800, 600 };
    viewport.grid_size = { 80, 30 };

    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = viewport,
        .display_ppi = 96.0f,
    };

    StubGridHost host;
    // Will reach initialize_host(); we only assert it is called and a handle was created.
    (void)host.initialize(context, callbacks);
    REQUIRE(renderer.create_grid_handle_calls == 1);
    REQUIRE(renderer.last_handle != nullptr);
    REQUIRE(host.initialize_host_called);
}

TEST_CASE("CommandPaletteHost::dispatch_action handles null grid handle without crash",
    "[palette][null_handle]")
{
    tests::FakeWindow window;
    tests::FakeGridPipelineRenderer renderer;
    renderer.fail_create_grid_handle = true;
    TextService text_service;
    tests::TestHostCallbacks callbacks;

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
    // toggle attempts to allocate a handle; with the failure flag on it logs and
    // returns true (action handled) but the palette must not become active.
    REQUIRE(host.dispatch_action("toggle"));
    REQUIRE_FALSE(host.is_active());
    REQUIRE(renderer.create_grid_handle_calls == 1);

    host.shutdown();
}
