#pragma once

#include "host_manager.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

struct WorkspaceSessionState
{
    int id = -1;
    std::string name;
    bool name_user_set = false;
    HostManager::SessionState host_manager;
};

struct AppSessionState
{
    int version = 1;
    std::string session_id = "default";
    int active_workspace_id = -1;
    int next_workspace_id = 0;
    std::vector<WorkspaceSessionState> workspaces;
};

std::filesystem::path default_session_state_path();
bool save_session_state(const AppSessionState& state, std::string* error = nullptr);
std::optional<AppSessionState> load_session_state(std::string* error = nullptr);

} // namespace draxul
