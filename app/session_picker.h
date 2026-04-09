#pragma once

#include "session_state.h"

#include <draxul/events.h>
#include <draxul/gui/palette_renderer.h>

#include <functional>
#include <string>
#include <vector>

namespace draxul
{

class SessionPicker
{
public:
    struct Deps
    {
        std::function<std::vector<SessionSummary>(std::string* error)> list_sessions;
        std::function<bool(std::string_view session_id, std::string* error)> activate_session;
        std::function<bool(std::string_view session_name, std::string* error)> create_session;
        std::function<bool(std::string_view session_id, std::string* error)> kill_session;
        std::function<void(std::string_view message)> report_error;
        std::function<void()> request_frame;
        std::function<void()> request_quit;
    };

    SessionPicker();
    explicit SessionPicker(Deps deps);

    void refresh_sessions();
    bool on_key(const KeyEvent& event);
    bool on_text_input(const TextInputEvent& event);
    gui::PaletteViewState view_state(int grid_cols, int grid_rows, float panel_bg_alpha = 1.0f);

private:
    enum class EntryKind
    {
        NewSession,
        Session,
    };

    struct FilteredEntry
    {
        EntryKind kind = EntryKind::Session;
        SessionSummary session;
        std::string name;
        std::string shortcut_hint;
        int score = 0;
        std::vector<size_t> match_positions;
    };

    void refilter(bool reset_selection);
    bool activate_selected();
    bool kill_selected();
    std::string trimmed_query() const;
    int default_selection_index() const;

    Deps deps_;
    std::string query_;
    int selected_index_ = 0;
    std::vector<SessionSummary> sessions_;
    std::vector<FilteredEntry> filtered_;
    std::vector<gui::PaletteEntry> view_entries_;
};

} // namespace draxul
