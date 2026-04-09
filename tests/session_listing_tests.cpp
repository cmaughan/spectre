#include "session_listing.h"
#include "session_state.h"
#include "split_tree.h"
#include "support/home_dir_redirect.h"
#include "support/temp_dir.h"

#include <catch2/catch_all.hpp>
#include <draxul/session_attach.h>

using namespace draxul;
using namespace draxul::tests;

namespace
{

AppSessionState make_saved_session(std::string session_id, std::string session_name)
{
    SplitTree tree;
    const LeafId leaf = tree.reset(800, 600);

    AppSessionState state;
    state.session_id = std::move(session_id);
    state.session_name = std::move(session_name);
    state.active_workspace_id = 1;
    state.next_workspace_id = 2;

    WorkspaceSessionState workspace;
    workspace.id = 1;
    workspace.name = "alpha";
    workspace.name_user_set = true;
    workspace.host_manager.tree = tree.snapshot();
    workspace.host_manager.panes.push_back({
        .leaf_id = leaf,
        .launch = {
            .kind = HostKind::PowerShell,
            .command = "pwsh",
            .working_dir = "D:/tmp",
        },
        .pane_name = "shell",
        .pane_id = "pane-1",
    });
    state.workspaces.push_back(std::move(workspace));
    return state;
}

} // namespace

TEST_CASE("session listing: live server summaries override saved counts", "[session_listing]")
{
    TempDir temp_dir("session-listing-live");
    HomeDirRedirect redirect(temp_dir.path);

    std::string error;
    REQUIRE(save_session_state(make_saved_session("alpha", "Alpha Session"), &error));
    REQUIRE(error.empty());

    SessionAttachServer server;
    REQUIRE(server.start(
        "alpha",
        [](SessionAttachServer::Command) {},
        []() {
            SessionAttachServer::LiveSessionInfo info;
            info.workspace_count = 3;
            info.pane_count = 7;
            info.detached = true;
            info.owner_pid = 5150;
            info.last_attached_unix_s = 111;
            info.last_detached_unix_s = 222;
            return info;
        }));

    const auto sessions = list_known_sessions(&error);
    REQUIRE(error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "alpha");
    CHECK(sessions[0].session_name == "Alpha Session");
    CHECK(sessions[0].live);
    CHECK(sessions[0].detached);
    CHECK(sessions[0].workspace_count == 3);
    CHECK(sessions[0].pane_count == 7);
    CHECK(sessions[0].owner_pid == 5150);
    CHECK(sessions[0].last_attached_unix_s == 111);
    CHECK(sessions[0].last_detached_unix_s == 222);

    server.stop();
}
