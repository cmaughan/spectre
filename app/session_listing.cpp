#include "session_listing.h"

#include <draxul/session_attach.h>

namespace draxul
{

std::vector<SessionSummary> list_known_sessions(std::string* error)
{
    std::string list_error;
    auto sessions = list_saved_sessions(&list_error);
    if (!list_error.empty())
    {
        if (error)
            *error = list_error;
        return {};
    }

    for (auto& session : sessions)
    {
        const auto probe_status = SessionAttachServer::probe(session.session_id);
        if (probe_status == SessionAttachServer::ProbeStatus::Running)
        {
            session.live = true;
            SessionAttachServer::LiveSessionInfo live_info;
            if (SessionAttachServer::query_live_session(session.session_id, &live_info))
            {
                session.detached = live_info.detached;
                session.workspace_count = live_info.workspace_count;
                session.pane_count = live_info.pane_count;
                session.owner_pid = live_info.owner_pid;
                session.last_attached_unix_s = live_info.last_attached_unix_s;
                session.last_detached_unix_s = live_info.last_detached_unix_s;
            }
        }
        else if (probe_status == SessionAttachServer::ProbeStatus::NoServer)
        {
            session.live = false;
        }
    }

    if (error)
        error->clear();
    return sessions;
}

} // namespace draxul
