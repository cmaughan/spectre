#include "session_picker.h"

#include <SDL3/SDL.h>
#include <catch2/catch_all.hpp>

using namespace draxul;

namespace
{

SessionSummary make_session(std::string id, std::string name)
{
    SessionSummary session;
    session.session_id = std::move(id);
    session.session_name = std::move(name);
    session.workspace_count = 2;
    session.pane_count = 4;
    session.has_saved_state = true;
    session.live = true;
    return session;
}

KeyEvent pressed_key(int keycode, ModifierFlags mod = 0)
{
    return KeyEvent{
        .scancode = 0,
        .keycode = keycode,
        .mod = mod,
        .pressed = true,
    };
}

} // namespace

TEST_CASE("session picker: existing session is selected by default", "[session_picker]")
{
    std::vector<SessionSummary> sessions = {
        make_session("alpha", "Alpha Session"),
    };

    SessionPicker picker(SessionPicker::Deps{
        .list_sessions = [&](std::string*) { return sessions; },
    });

    picker.refresh_sessions();
    const auto state = picker.view_state(80, 20);
    REQUIRE(state.entries.size() == 2);
    CHECK(state.selected_index == 1);
    CHECK(state.entries[0].name == "new-session");
    CHECK(state.entries[1].name == "Alpha Session (alpha)");
}

TEST_CASE("session picker: Enter on the new-session row creates a named session", "[session_picker]")
{
    std::string created_name;
    int quit_requests = 0;

    SessionPicker picker(SessionPicker::Deps{
        .list_sessions = [](std::string*) { return std::vector<SessionSummary>{}; },
        .create_session = [&](std::string_view session_name, std::string*) {
            created_name = std::string(session_name);
            return true;
        },
        .request_quit = [&]() { ++quit_requests; },
    });

    picker.refresh_sessions();
    picker.on_text_input({ .text = "Work Bench" });
    REQUIRE(picker.on_key(pressed_key(SDLK_RETURN)));

    CHECK(created_name == "Work Bench");
    CHECK(quit_requests == 1);
}

TEST_CASE("session picker: Delete kills the selected session and refreshes the list", "[session_picker]")
{
    std::vector<SessionSummary> sessions = {
        make_session("alpha", "Alpha Session"),
    };
    std::string killed_id;

    SessionPicker picker(SessionPicker::Deps{
        .list_sessions = [&](std::string*) { return sessions; },
        .kill_session = [&](std::string_view session_id, std::string*) {
            killed_id = std::string(session_id);
            sessions.clear();
            return true;
        },
    });

    picker.refresh_sessions();
    REQUIRE(picker.on_key(pressed_key(SDLK_DELETE)));

    const auto state = picker.view_state(80, 20);
    CHECK(killed_id == "alpha");
    REQUIRE(state.entries.size() == 1);
    CHECK(state.entries[0].name == "new-session");
}

TEST_CASE("session picker: Enter on an existing session activates it", "[session_picker]")
{
    std::vector<SessionSummary> sessions = {
        make_session("alpha", "Alpha Session"),
    };
    std::string activated_id;
    int quit_requests = 0;

    SessionPicker picker(SessionPicker::Deps{
        .list_sessions = [&](std::string*) { return sessions; },
        .activate_session = [&](std::string_view session_id, std::string*) {
            activated_id = std::string(session_id);
            return true;
        },
        .request_quit = [&]() { ++quit_requests; },
    });

    picker.refresh_sessions();
    REQUIRE(picker.on_key(pressed_key(SDLK_RETURN)));

    CHECK(activated_id == "alpha");
    CHECK(quit_requests == 1);
}
