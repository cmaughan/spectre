#include "session_state.h"
#include "split_tree.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

TEST_CASE("session state: save/load round-trip preserves workspace topology", "[session_state]")
{
    TempDir temp_dir("session-state-roundtrip");
    HomeDirRedirect redirect(temp_dir.path);

    SplitTree tree;
    const LeafId left = tree.reset(1200, 800);
    const LeafId right = tree.split_leaf(left, SplitDirection::Vertical);
    tree.set_focused(right);

    HostManager::SessionState host_manager_state;
    host_manager_state.tree = tree.snapshot();
    host_manager_state.zoomed = true;
    host_manager_state.zoomed_leaf = right;
    host_manager_state.panes.push_back({
        .leaf_id = left,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .args = { "-NoLogo" },
            .working_dir = "D:/left",
            .source_path = "",
            .startup_commands = { "echo left" },
        },
        .pane_name = "left",
    });
    host_manager_state.panes.push_back({
        .leaf_id = right,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .args = { "-NoProfile" },
            .working_dir = "D:/right",
            .source_path = "",
            .startup_commands = { "echo right" },
        },
        .pane_name = "right",
    });

    WorkspaceSessionState workspace;
    workspace.id = 7;
    workspace.name = "session";
    workspace.name_user_set = true;
    workspace.host_manager = std::move(host_manager_state);

    AppSessionState state;
    state.active_workspace_id = 7;
    state.next_workspace_id = 8;
    state.workspaces.push_back(std::move(workspace));

    std::string save_error;
    REQUIRE(save_session_state(state, &save_error));
    REQUIRE(save_error.empty());

    std::string load_error;
    auto loaded = load_session_state(&load_error);
    REQUIRE(loaded);
    REQUIRE(load_error.empty());
    REQUIRE(loaded->active_workspace_id == 7);
    REQUIRE(loaded->next_workspace_id == 8);
    REQUIRE(loaded->workspaces.size() == 1);

    const WorkspaceSessionState& loaded_workspace = loaded->workspaces.front();
    CHECK(loaded_workspace.id == 7);
    CHECK(loaded_workspace.name == "session");
    CHECK(loaded_workspace.name_user_set);
    REQUIRE(loaded_workspace.host_manager.panes.size() == 2);
    CHECK(loaded_workspace.host_manager.zoomed);
    CHECK(loaded_workspace.host_manager.zoomed_leaf == right);

    SplitTree restored_tree;
    REQUIRE(restored_tree.restore(loaded_workspace.host_manager.tree, 1200, 800));
    CHECK(restored_tree.leaf_count() == 2);
    CHECK(restored_tree.focused() == right);
    CHECK(restored_tree.descriptor_for(left).pixel_size.x == tree.descriptor_for(left).pixel_size.x);
    CHECK(restored_tree.descriptor_for(right).pixel_pos.x == tree.descriptor_for(right).pixel_pos.x);

    CHECK(loaded_workspace.host_manager.panes[0].pane_name == "left");
    CHECK(loaded_workspace.host_manager.panes[0].launch.working_dir == "D:/left");
    CHECK(loaded_workspace.host_manager.panes[1].pane_name == "right");
    CHECK(loaded_workspace.host_manager.panes[1].launch.args == (std::vector<std::string>{ "-NoProfile" }));
}
