#include <catch2/catch_all.hpp>

#include "support/fake_renderer.h"
#include "support/fake_window.h"
#include "support/test_host_callbacks.h"

#include <draxul/app_config.h>
#include <draxul/app_options.h>
#include <draxul/text_service.h>

#include "host_manager.h"
#include "split_tree.h"
#include <draxul/grid_host_base.h>
#ifdef DRAXUL_ENABLE_MEGACITY
#include <draxul/megacity_host.h>
#endif

using namespace draxul;
using namespace draxul::tests;

namespace
{

std::string bundled_font_path()
{
    return std::string(DRAXUL_PROJECT_ROOT) + "/fonts/JetBrainsMonoNerdFont-Regular.ttf";
}

class LifetimeTestHost final : public IHost
{
public:
    explicit LifetimeTestHost(std::shared_ptr<int> shutdown_calls)
        : shutdown_calls_(std::move(shutdown_calls))
    {
    }

    bool initialize(const HostContext&, IHostCallbacks& callbacks) override
    {
        callbacks_ = &callbacks;
        return true;
    }

    void shutdown() override
    {
        if (shutdown_calls_)
            ++(*shutdown_calls_);
        if (fire_callback_on_shutdown_ && callbacks_)
            callbacks_->request_frame();
        running_ = false;
    }

    bool is_running() const override
    {
        return running_;
    }

    std::string init_error() const override
    {
        return {};
    }

    void set_viewport(const HostViewport&) override {}
    void pump() override {}
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override
    {
        return std::nullopt;
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override
    {
        running_ = false;
    }

    Color default_background() const override
    {
        return Color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    HostRuntimeState runtime_state() const override
    {
        HostRuntimeState state;
        state.content_ready = true;
        return state;
    }

    HostDebugState debug_state() const override
    {
        HostDebugState state;
        state.name = "lifetime-test";
        return state;
    }

    void trigger_frame_request() const
    {
        REQUIRE(callbacks_ != nullptr);
        callbacks_->request_frame();
    }

    void set_fire_callback_on_shutdown(bool enabled)
    {
        fire_callback_on_shutdown_ = enabled;
    }

    int shutdown_calls() const
    {
        return shutdown_calls_ ? *shutdown_calls_ : 0;
    }

private:
    IHostCallbacks* callbacks_ = nullptr;
    std::shared_ptr<int> shutdown_calls_;
    bool running_ = true;
    bool fire_callback_on_shutdown_ = false;
};

class GuardedGridHost final : public GridHostBase
{
public:
    bool initialize_host() override
    {
        return true;
    }

    void on_viewport_changed() override {}

    void on_font_metrics_changed_impl() override {}

    std::string_view host_name() const override
    {
        return "guarded-grid-host";
    }

    void pump() override {}

    void shutdown() override {}

    bool is_running() const override
    {
        return true;
    }

    std::string init_error() const override
    {
        return {};
    }

    bool dispatch_action(std::string_view) override
    {
        return false;
    }

    void request_close() override {}

    void exercise_apply_grid_size(int cols, int rows)
    {
        apply_grid_size(cols, rows);
    }
};

struct HostManagerHarness
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestHostCallbacks callbacks;
    AppOptions options;
    AppConfig config;
    float display_ppi = 96.0f;
    std::vector<LifetimeTestHost*> created_hosts;
    std::vector<std::shared_ptr<int>> shutdown_counters;
    HostManager manager;

    HostManagerHarness()
        : manager(make_deps())
    {
        TextServiceConfig ts_cfg;
        ts_cfg.font_path = bundled_font_path();
        const bool ok = text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, display_ppi);
        REQUIRE(ok);
    }

    HostManager::Deps make_deps()
    {
        options.load_user_config = false;
        options.save_user_config = false;
        options.host_kind = HostKind::Nvim;
        options.host_factory = [this](HostKind) -> std::unique_ptr<IHost> {
            auto shutdown_calls = std::make_shared<int>(0);
            shutdown_counters.push_back(shutdown_calls);
            auto host = std::make_unique<LifetimeTestHost>(std::move(shutdown_calls));
            created_hosts.push_back(host.get());
            return host;
        };

        HostManager::Deps deps;
        deps.options = &options;
        deps.config = &config;
        deps.window = &window;
        deps.grid_renderer = &renderer;
        deps.text_service = &text_service;
        deps.display_ppi = &display_ppi;
        deps.compute_viewport = [](const PaneDescriptor& desc) {
            HostViewport viewport;
            viewport.pixel_pos = desc.pixel_pos;
            viewport.pixel_size = desc.pixel_size;
            viewport.grid_size = { 80, 24 };
            return viewport;
        };
        return deps;
    }
};

} // namespace

TEST_CASE("host manager: split panes use the platform shell for non-shell primary hosts", "[host_manager]")
{
#ifdef _WIN32
    constexpr HostKind expected = HostKind::PowerShell;
#else
    constexpr HostKind expected = HostKind::Zsh;
#endif

    REQUIRE(HostManager::platform_default_split_host_kind() == expected);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Nvim) == expected);
    REQUIRE(HostManager::split_host_kind_for(HostKind::MegaCity) == expected);
}

TEST_CASE("host manager: split panes preserve explicit shell host choices", "[host_manager]")
{
    REQUIRE(HostManager::split_host_kind_for(HostKind::PowerShell) == HostKind::PowerShell);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Bash) == HostKind::Bash);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Zsh) == HostKind::Zsh);
    REQUIRE(HostManager::split_host_kind_for(HostKind::Wsl) == HostKind::Wsl);
}

// --- SplitTree-level lifecycle tests ---
// These test the SplitTree that HostManager uses for layout and lifecycle.

TEST_CASE("split tree: reset creates single leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    REQUIRE(root != kInvalidLeaf);
    REQUIRE(tree.leaf_count() == 1);
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: split creates two leaves with correct viewports", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Vertical);

    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(tree.leaf_count() == 2);

    PaneDescriptor d1 = tree.descriptor_for(root);
    PaneDescriptor d2 = tree.descriptor_for(new_leaf);

    // Both panes should have non-zero dimensions
    REQUIRE(d1.pixel_size.x > 0);
    REQUIRE(d1.pixel_size.y > 0);
    REQUIRE(d2.pixel_size.x > 0);
    REQUIRE(d2.pixel_size.y > 0);

    // Combined widths should approximately equal total (minus divider)
    INFO("vertical split divides width between panes");
    REQUIRE(d1.pixel_size.x + d2.pixel_size.x + SplitTree::kDividerWidth <= 800);
}

TEST_CASE("split tree: horizontal split divides height", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Horizontal);

    REQUIRE(new_leaf != kInvalidLeaf);

    PaneDescriptor d1 = tree.descriptor_for(root);
    PaneDescriptor d2 = tree.descriptor_for(new_leaf);

    INFO("horizontal split divides height between panes");
    REQUIRE(d1.pixel_size.y + d2.pixel_size.y + SplitTree::kDividerWidth <= 600);
    INFO("horizontal split preserves full width for both panes");
    REQUIRE(d1.pixel_size.x == 800);
    REQUIRE(d2.pixel_size.x == 800);
}

TEST_CASE("split tree: close leaf collapses tree and reassigns focus", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId new_leaf = tree.split_leaf(root, SplitDirection::Vertical);

    tree.set_focused(new_leaf);
    REQUIRE(tree.focused() == new_leaf);

    bool closed = tree.close_leaf(new_leaf);
    REQUIRE(closed);
    REQUIRE(tree.leaf_count() == 1);

    // Focus should be reassigned to the remaining leaf
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: cannot close last leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);

    bool closed = tree.close_leaf(root);
    REQUIRE_FALSE(closed);
    REQUIRE(tree.leaf_count() == 1);
}

TEST_CASE("split tree: hit test returns correct leaf", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId right = tree.split_leaf(root, SplitDirection::Vertical);

    PaneDescriptor d_left = tree.descriptor_for(root);
    PaneDescriptor d_right = tree.descriptor_for(right);

    // Hit test in the left pane
    auto result_left = tree.hit_test(d_left.pixel_pos.x + 10, d_left.pixel_pos.y + 10);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result_left));
    REQUIRE(std::get<SplitTree::LeafHit>(result_left).id == root);

    // Hit test in the right pane
    auto result_right = tree.hit_test(d_right.pixel_pos.x + 10, d_right.pixel_pos.y + 10);
    REQUIRE(std::holds_alternative<SplitTree::LeafHit>(result_right));
    REQUIRE(std::get<SplitTree::LeafHit>(result_right).id == right);
}

TEST_CASE("split tree: recompute updates all descriptors proportionally", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId right = tree.split_leaf(root, SplitDirection::Vertical);

    PaneDescriptor d1_before = tree.descriptor_for(root);

    // Resize the window to 1600x1200
    tree.recompute(1600, 1200);

    PaneDescriptor d1_after = tree.descriptor_for(root);
    PaneDescriptor d2_after = tree.descriptor_for(right);

    INFO("recomputed pane is wider than before");
    REQUIRE(d1_after.pixel_size.x > d1_before.pixel_size.x);
    INFO("recomputed panes fit in new window dimensions");
    REQUIRE(d1_after.pixel_size.x + d2_after.pixel_size.x + SplitTree::kDividerWidth <= 1600);
    INFO("height adjusts to new window height");
    REQUIRE(d1_after.pixel_size.y == 1200);
}

TEST_CASE("split tree: for_each_leaf visits all leaves", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);
    LeafId third = tree.split_leaf(second, SplitDirection::Horizontal);

    std::vector<LeafId> visited;
    tree.for_each_leaf([&](LeafId id, const PaneDescriptor&) {
        visited.push_back(id);
    });

    REQUIRE(visited.size() == 3);
    // All three IDs should be present
    REQUIRE(std::find(visited.begin(), visited.end(), root) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), second) != visited.end());
    REQUIRE(std::find(visited.begin(), visited.end(), third) != visited.end());
}

TEST_CASE("split tree: focus changes via set_focused", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);

    REQUIRE(tree.focused() == root);

    tree.set_focused(second);
    REQUIRE(tree.focused() == second);

    tree.set_focused(root);
    REQUIRE(tree.focused() == root);
}

TEST_CASE("split tree: double split creates three panes", "[host_manager]")
{
    SplitTree tree;
    LeafId root = tree.reset(800, 600);
    LeafId second = tree.split_leaf(root, SplitDirection::Vertical);
    LeafId third = tree.split_leaf(root, SplitDirection::Horizontal);

    REQUIRE(tree.leaf_count() == 3);

    // All descriptors should have non-zero dimensions
    REQUIRE(tree.descriptor_for(root).pixel_size.x > 0);
    REQUIRE(tree.descriptor_for(second).pixel_size.x > 0);
    REQUIRE(tree.descriptor_for(third).pixel_size.x > 0);
}

TEST_CASE("host manager: callbacks remain valid across pane teardown", "[host_manager]")
{
    HostManagerHarness harness;

    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.created_hosts.size() == 1);

    const LeafId new_leaf = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(new_leaf != kInvalidLeaf);
    REQUIRE(harness.created_hosts.size() == 2);

    auto* primary = harness.created_hosts[0];
    auto* secondary = harness.created_hosts[1];
    REQUIRE(primary != nullptr);
    REQUIRE(secondary != nullptr);

    secondary->set_fire_callback_on_shutdown(true);
    REQUIRE(harness.manager.close_leaf(new_leaf));
    REQUIRE(harness.callbacks.request_frame_calls == 1);
    REQUIRE(*harness.shutdown_counters[1] == 1);

    primary->trigger_frame_request();
    REQUIRE(harness.callbacks.request_frame_calls == 2);
}

TEST_CASE("grid host base: invalidated owner lifetime blocks renderer and callback use", "[host_manager]")
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TestHostCallbacks callbacks;
    TextService text_service;
    TextServiceConfig ts_cfg;
    ts_cfg.font_path = bundled_font_path();
    REQUIRE(text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, 96.0f));

    auto owner_lifetime = std::make_shared<int>(0);
    HostViewport viewport;
    viewport.pixel_size = { 640, 480 };
    viewport.grid_size = { 80, 24 };

    GuardedGridHost host;
    HostContext context{
        .window = &window,
        .grid_renderer = &renderer,
        .text_service = &text_service,
        .initial_viewport = viewport,
        .owner_lifetime = owner_lifetime,
    };
    REQUIRE(host.initialize(context, callbacks));

    const int baseline_set_cell_size_calls = renderer.set_cell_size_calls;
    owner_lifetime.reset();

    REQUIRE_NOTHROW(host.on_font_metrics_changed());
    REQUIRE(renderer.set_cell_size_calls == baseline_set_cell_size_calls);

    callbacks.request_frame_calls = 0;
    REQUIRE_NOTHROW(host.exercise_apply_grid_size(100, 40));
    REQUIRE(callbacks.request_frame_calls == 0);

    callbacks.last_text_input_area = { 7, 8, 9, 10 };
    REQUIRE_NOTHROW(host.set_viewport(viewport));
    REQUIRE(callbacks.last_text_input_area.x == 7);
    REQUIRE(callbacks.last_text_input_area.y == 8);
    REQUIRE(callbacks.last_text_input_area.w == 9);
    REQUIRE(callbacks.last_text_input_area.h == 10);

    text_service.shutdown();
}

// ---------------------------------------------------------------------------
// Pane zoom + close interaction tests (WI 65)
// ---------------------------------------------------------------------------
//
// Pin down toggle_zoom() / close_leaf() interactions:
//   * close the zoomed pane → zoom state cleared, surviving pane fills viewport
//   * close a non-zoomed pane in a 3-pane tree → still zoomed, viewport intact
//   * close in a 2-pane tree (whether zoomed pane or not) → zoom auto-cleared
//   * toggle zoom on/off → original layout restored
//   * single-pane toggle is a no-op (no zoom state set)
//   * focus navigation references the surviving pane after a zoomed-pane close

TEST_CASE("zoom: toggle on/off restores original two-pane layout", "[host_manager][zoom]")
{
    HostManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(right != kInvalidLeaf);

    const PaneDescriptor before_left = harness.manager.tree().descriptor_for(left);
    const PaneDescriptor before_right = harness.manager.tree().descriptor_for(right);

    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);

    harness.manager.toggle_zoom(800, 600);
    REQUIRE_FALSE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == kInvalidLeaf);

    // Layout fully restored.
    const PaneDescriptor after_left = harness.manager.tree().descriptor_for(left);
    const PaneDescriptor after_right = harness.manager.tree().descriptor_for(right);
    CHECK(after_left.pixel_size.x == before_left.pixel_size.x);
    CHECK(after_left.pixel_size.y == before_left.pixel_size.y);
    CHECK(after_right.pixel_size.x == before_right.pixel_size.x);
    CHECK(after_right.pixel_size.y == before_right.pixel_size.y);
}

TEST_CASE("zoom: closing the zoomed pane clears zoom state", "[host_manager][zoom]")
{
    HostManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    const LeafId third = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    REQUIRE(third != kInvalidLeaf);
    REQUIRE(harness.manager.host_count() == 3);

    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);

    REQUIRE(harness.manager.close_leaf(right));
    REQUIRE_FALSE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == kInvalidLeaf);
    REQUIRE(harness.manager.host_count() == 2);

    // Surviving panes have valid viewports.
    CHECK(harness.manager.tree().descriptor_for(left).pixel_size.x > 0);
    CHECK(harness.manager.tree().descriptor_for(third).pixel_size.x > 0);
}

TEST_CASE("zoom: closing a non-zoomed pane in a 3-pane tree leaves the other zoomed",
    "[host_manager][zoom]")
{
    // close_leaf() inspects leaf_count() *before* the close: with 3 panes pre-close
    // (leaf_count()==3 > 2), the auto-cancel branch does not fire, so zoom survives.
    HostManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    const LeafId third = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    REQUIRE(harness.manager.host_count() == 3);

    // Zoom the right pane, then close a different pane (third).
    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);

    REQUIRE(harness.manager.close_leaf(third));
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == right);
    REQUIRE(harness.manager.host_count() == 2);
    CHECK(harness.manager.tree().descriptor_for(left).pixel_size.x > 0);
    CHECK(harness.manager.tree().descriptor_for(right).pixel_size.x > 0);
}

TEST_CASE("zoom: closing the last sibling in a 2-pane tree auto-clears zoom",
    "[host_manager][zoom]")
{
    // With 2 panes pre-close, leaf_count()==2 <= 2 trips the defensive cancel,
    // even when the closed pane is NOT the zoomed one.
    HostManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    REQUIRE(harness.manager.host_count() == 2);

    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());

    REQUIRE(harness.manager.close_leaf(left));
    REQUIRE_FALSE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.host_count() == 1);
}

TEST_CASE("zoom: closing a non-zoomed pane in a 4-pane tree preserves zoom",
    "[host_manager][zoom]")
{
    // Same as the previous test but with a fourth pane so the close does NOT
    // trip the "drop to 2 panes" auto-cancel — verifies the zoomed pane stays
    // zoomed when there is still ambient layout to preserve.
    HostManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId a = harness.manager.focused_leaf();
    const LeafId b = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);
    harness.manager.set_focused(a);
    const LeafId c = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    harness.manager.set_focused(b);
    const LeafId d = harness.manager.split_focused(SplitDirection::Horizontal, harness.callbacks);
    REQUIRE(harness.manager.host_count() == 4);

    // Zoom pane B, then close pane D (a different, non-zoomed pane).
    harness.manager.set_focused(b);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == b);

    REQUIRE(harness.manager.close_leaf(d));
    REQUIRE(harness.manager.is_zoomed());
    REQUIRE(harness.manager.zoomed_leaf() == b);
    REQUIRE(harness.manager.host_count() == 3);
    CHECK(harness.manager.tree().descriptor_for(c).pixel_size.x > 0);
}

TEST_CASE("zoom: toggle is a no-op on a single-pane tree", "[host_manager][zoom]")
{
    HostManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    REQUIRE(harness.manager.host_count() == 1);

    harness.manager.toggle_zoom(800, 600);
    CHECK_FALSE(harness.manager.is_zoomed());
    CHECK(harness.manager.zoomed_leaf() == kInvalidLeaf);
}

TEST_CASE("zoom: focus navigation after zoomed-pane close lands on a valid host",
    "[host_manager][zoom]")
{
    HostManagerHarness harness;
    REQUIRE(harness.manager.create(harness.callbacks, 800, 600));
    const LeafId left = harness.manager.focused_leaf();
    const LeafId right = harness.manager.split_focused(SplitDirection::Vertical, harness.callbacks);

    harness.manager.set_focused(right);
    harness.manager.toggle_zoom(800, 600);
    REQUIRE(harness.manager.close_leaf(right));

    // The surviving leaf must be focused and reachable through focused_host().
    REQUIRE(harness.manager.focused_leaf() == left);
    REQUIRE(harness.manager.focused_host() != nullptr);
    REQUIRE(harness.manager.host_for(left) == harness.manager.focused_host());
}

#ifdef DRAXUL_ENABLE_MEGACITY
TEST_CASE("host manager: MegaCity continuous refresh option enables idle deadlines", "[host_manager][megacity]")
{
    FakeWindow window;
    FakeTermRenderer renderer;
    TextService text_service;
    TestHostCallbacks callbacks;
    AppOptions options;
    AppConfig config;
    float display_ppi = 96.0f;

    TextServiceConfig ts_cfg;
    ts_cfg.font_path = bundled_font_path();
    REQUIRE(text_service.initialize(ts_cfg, TextService::DEFAULT_POINT_SIZE, display_ppi));

    options.load_user_config = false;
    options.save_user_config = false;
    options.host_kind = HostKind::MegaCity;
    options.request_continuous_refresh = true;

    HostManager::Deps deps;
    deps.options = &options;
    deps.config = &config;
    deps.window = &window;
    deps.grid_renderer = &renderer;
    deps.text_service = &text_service;
    deps.display_ppi = &display_ppi;
    deps.compute_viewport = [](const PaneDescriptor& desc) {
        HostViewport viewport;
        viewport.pixel_pos = desc.pixel_pos;
        viewport.pixel_size = desc.pixel_size;
        viewport.grid_size = { 1, 1 };
        return viewport;
    };

    HostManager manager(std::move(deps));
    REQUIRE(manager.create(callbacks, 800, 600));

    auto* megacity = dynamic_cast<MegaCityHost*>(manager.focused_host());
    REQUIRE(megacity != nullptr);
    CHECK(megacity->next_deadline().has_value());

    manager.shutdown();
}
#endif
