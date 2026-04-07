#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <string>

namespace draxul::sdl
{

// Launch the async open-file dialog. On selection, a custom SDL event of type
// result_event_type is pushed to the main event queue; consume it by passing
// subsequent events to handle_file_dialog_event().
void show_open_file_dialog(SDL_Window* window, Uint32 result_event_type);

// Returns true if event is a file-dialog result event (consumed).
// Calls on_path with the selected path when a file was chosen.
bool handle_file_dialog_event(const SDL_Event& event,
    Uint32 file_dialog_event_type,
    const std::function<void(const std::string&)>& on_path);

} // namespace draxul::sdl
