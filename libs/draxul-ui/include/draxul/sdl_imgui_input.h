#pragma once

#include <imgui.h>

namespace draxul
{

// Maps an SDL scancode integer to the corresponding ImGuiKey.
// Returns ImGuiKey_None if the scancode has no ImGui equivalent.
// Takes int rather than SDL_Scancode to avoid leaking SDL headers into callers.
ImGuiKey sdl_scancode_to_imgui_key(int scancode);

} // namespace draxul
