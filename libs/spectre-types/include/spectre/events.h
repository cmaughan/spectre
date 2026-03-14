#pragma once
#include <cstdint>

namespace spectre {

// Event types for window callbacks
struct WindowResizeEvent { int width, height; };
struct KeyEvent { int scancode; int keycode; uint16_t mod; bool pressed; };
struct TextInputEvent { const char* text; };
struct MouseButtonEvent { int button; bool pressed; int x, y; };
struct MouseMoveEvent { int x, y; };
struct MouseWheelEvent { float dx, dy; int x, y; };

} // namespace spectre
