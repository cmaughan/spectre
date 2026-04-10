#pragma once

#include "host_manager.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
    std::string session_name = "default";
    int active_workspace_id = -1;
    int next_workspace_id = 0;
    std::vector<WorkspaceSessionState> workspaces;
};

struct SessionRuntimeMetadata
{
    int version = 1;
    std::string session_id = "default";
    std::string session_name = "default";
    bool live = false;
    bool detached = false;
    uint64_t owner_pid = 0;
    int64_t last_attached_unix_s = 0;
    int64_t last_detached_unix_s = 0;
};

struct SessionSummary
{
    std::string session_id;
    std::string session_name;
    int workspace_count = 0;
    int pane_count = 0;
    bool has_saved_state = false;
    bool live = false;
    bool detached = false;
    uint64_t owner_pid = 0;
    int64_t last_attached_unix_s = 0;
    int64_t last_detached_unix_s = 0;
};

std::filesystem::path session_state_directory();
std::filesystem::path session_state_path(std::string_view session_id);
std::filesystem::path session_metadata_path(std::string_view session_id);
bool has_saved_session_state(std::string_view session_id, std::string* error = nullptr);
bool save_session_state(const AppSessionState& state, std::string* error = nullptr);
bool delete_session_state(std::string_view session_id, std::string* error = nullptr);
bool save_session_runtime_metadata(
    const SessionRuntimeMetadata& metadata, std::string* error = nullptr);
bool delete_session_runtime_metadata(std::string_view session_id, std::string* error = nullptr);
bool clear_session_runtime_liveness(std::string_view session_id, std::string* error = nullptr);
std::optional<SessionRuntimeMetadata> load_session_runtime_metadata(
    std::string_view session_id, std::string* error = nullptr);
std::optional<AppSessionState> load_session_state(
    std::string_view session_id, std::string* error = nullptr);
std::optional<AppSessionState> load_session_state(std::string* error = nullptr);
std::vector<SessionSummary> list_saved_sessions(std::string* error = nullptr);

} // namespace draxul
