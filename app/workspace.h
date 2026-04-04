#pragma once

#include "host_manager.h"
#include <string>

namespace draxul
{

// A workspace is a self-contained pane layout: one SplitTree + its hosts.
// ChromeHost owns a collection of workspaces (one per tab).
struct Workspace
{
    int id = -1;
    std::string name;
    HostManager host_manager;
    bool initialized = false;

    explicit Workspace(int workspace_id, HostManager::Deps deps)
        : id(workspace_id)
        , host_manager(std::move(deps))
    {
    }
};

} // namespace draxul
