#pragma once
#include <string>

namespace draxul::sdl
{

std::string get_clipboard_text();
bool set_clipboard_text(const std::string& text);

} // namespace draxul::sdl
