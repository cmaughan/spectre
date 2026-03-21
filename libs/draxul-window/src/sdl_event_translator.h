#pragma once
#include <SDL3/SDL.h>
#include <draxul/events.h>
#include <optional>
#include <string>

namespace draxul::sdl
{

// Translate individual SDL events into draxul event types.
// Each function returns nullopt if the SDL event type does not match.
// Resize and display-scale functions require the SDL_Window* to query current state.

std::optional<KeyEvent> translate_key(const SDL_Event& event);
std::optional<TextInputEvent> translate_text_input(const SDL_Event& event);
std::optional<TextEditingEvent> translate_text_editing(const SDL_Event& event);
std::optional<MouseButtonEvent> translate_mouse_button(const SDL_Event& event);
std::optional<MouseMoveEvent> translate_mouse_move(const SDL_Event& event);
std::optional<MouseWheelEvent> translate_mouse_wheel(const SDL_Event& event);
std::optional<WindowResizeEvent> translate_resize(SDL_Window* window, const SDL_Event& event);
std::optional<DisplayScaleEvent> translate_display_scale(SDL_Window* window, const SDL_Event& event);
std::optional<std::string> translate_file_drop(const SDL_Event& event);

} // namespace draxul::sdl
