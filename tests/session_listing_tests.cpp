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

TEST_CASE("session listing: formatter prints aligned console table", "[session_listing]")
{
    SessionSummary alpha;
    alpha.session_id = "alpha";
    alpha.session_name = "Alpha Session";
    alpha.live = true;
    alpha.detached = true;
    alpha.workspace_count = 3;
    alpha.pane_count = 12;
    alpha.has_saved_state = true;

    SessionSummary beta;
    beta.session_id = "beta";
    beta.session_name = "beta";
    beta.live = false;
    beta.detached = false;
    beta.workspace_count = 1;
    beta.pane_count = 2;
    beta.has_saved_state = true;

    const std::string table = format_session_listing_table({ alpha, beta });
    const std::string expected
        = "SESSION ID  STATE     WORKSPACES  PANES  NAME\n"
          "----------  --------  ----------  -----  ----\n"
          "alpha       detached           3     12  Alpha Session\n"
          "beta        saved              1      2  \n";

    CHECK(table == expected);
}

TEST_CASE("session listing: stale metadata is scrubbed when no live server exists", "[session_listing]")
{
    TempDir temp_dir("session-listing-stale");
    HomeDirRedirect redirect(temp_dir.path);

    std::string error;
    REQUIRE(save_session_state(make_saved_session("alpha", "Alpha Session"), &error));
    REQUIRE(error.empty());

    SessionRuntimeMetadata metadata;
    metadata.session_id = "alpha";
    metadata.session_name = "Alpha Session";
    metadata.live = true;
    metadata.detached = true;
    metadata.owner_pid = 5150;
    metadata.last_attached_unix_s = 111;
    metadata.last_detached_unix_s = 222;
    REQUIRE(save_session_runtime_metadata(metadata, &error));
    REQUIRE(error.empty());

    const auto sessions = list_known_sessions(&error);
    REQUIRE(error.empty());
    REQUIRE(sessions.size() == 1);
    CHECK(sessions[0].session_id == "alpha");
    CHECK_FALSE(sessions[0].live);
    CHECK_FALSE(sessions[0].detached);
    CHECK(sessions[0].owner_pid == 0);
    CHECK(sessions[0].has_saved_state);
    CHECK(sessions[0].last_attached_unix_s == 111);
    CHECK(sessions[0].last_detached_unix_s == 222);

    auto scrubbed = load_session_runtime_metadata("alpha", &error);
    REQUIRE(scrubbed);
    REQUIRE(error.empty());
    CHECK_FALSE(scrubbed->live);
    CHECK_FALSE(scrubbed->detached);
    CHECK(scrubbed->owner_pid == 0);
    CHECK(scrubbed->last_attached_unix_s == 111);
    CHECK(scrubbed->last_detached_unix_s == 222);
}

TEST_CASE("session listing: stale metadata-only sessions are hidden", "[session_listing]")
{
    TempDir temp_dir("session-listing-stale-metadata-only");
    HomeDirRedirect redirect(temp_dir.path);

    SessionRuntimeMetadata metadata;
    metadata.session_id = "ghost";
    metadata.session_name = "Ghost Session";
    metadata.live = true;
    metadata.detached = true;
    metadata.owner_pid = 4242;
    metadata.last_attached_unix_s = 111;
    metadata.last_detached_unix_s = 222;

    std::string error;
    REQUIRE(save_session_runtime_metadata(metadata, &error));
    REQUIRE(error.empty());

    const auto sessions = list_known_sessions(&error);
    REQUIRE(error.empty());
    CHECK(sessions.empty());

    auto scrubbed = load_session_runtime_metadata("ghost", &error);
    REQUIRE(scrubbed);
    REQUIRE(error.empty());
    CHECK_FALSE(scrubbed->live);
    CHECK_FALSE(scrubbed->detached);
    CHECK(scrubbed->owner_pid == 0);
}
