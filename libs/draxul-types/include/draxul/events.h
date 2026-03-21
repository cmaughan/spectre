#pragma once
#include <draxul/input_types.h>

namespace draxul
{

// Event types for window callbacks
struct WindowResizeEvent
{
    int width, height;
};
struct KeyEvent
{
    int scancode;
    int keycode;
    ModifierFlags mod;
    bool pressed;
};
struct TextInputEvent
{
    const char* text;
};
struct TextEditingEvent
{
    const char* text;
    int start = 0;
    int length = 0;
};
struct MouseButtonEvent
{
    int button;
    bool pressed;
    ModifierFlags mod;
    int x, y;
};
struct MouseMoveEvent
{
    ModifierFlags mod;
    int x, y;
};
struct MouseWheelEvent
{
    float dx, dy;
    ModifierFlags mod;
    int x, y;
};
struct DisplayScaleEvent
{
    float display_ppi; // new effective PPI (96 * display_scale)
};

} // namespace draxul
