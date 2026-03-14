#include <spectre/nvim.h>
#include <SDL3/SDL.h>
#include <cstdio>

namespace spectre {

void NvimInput::initialize(NvimRpc* rpc, int cell_w, int cell_h) {
    rpc_ = rpc;
    cell_w_ = cell_w;
    cell_h_ = cell_h;
}

void NvimInput::send_input(const std::string& keys) {
    if (keys.empty()) return;
    rpc_->notify("nvim_input", { NvimRpc::make_str(keys) });
}

std::string NvimInput::translate_key(int keycode, uint16_t mod) {
    std::string key;

    switch (keycode) {
    case SDLK_ESCAPE:    key = "Esc"; break;
    case SDLK_RETURN:    key = "CR"; break;
    case SDLK_TAB:       key = "Tab"; break;
    case SDLK_BACKSPACE: key = "BS"; break;
    case SDLK_DELETE:    key = "Del"; break;
    case SDLK_INSERT:    key = "Insert"; break;
    case SDLK_UP:        key = "Up"; break;
    case SDLK_DOWN:      key = "Down"; break;
    case SDLK_LEFT:      key = "Left"; break;
    case SDLK_RIGHT:     key = "Right"; break;
    case SDLK_HOME:      key = "Home"; break;
    case SDLK_END:       key = "End"; break;
    case SDLK_PAGEUP:    key = "PageUp"; break;
    case SDLK_PAGEDOWN:  key = "PageDown"; break;
    case SDLK_F1:        key = "F1"; break;
    case SDLK_F2:        key = "F2"; break;
    case SDLK_F3:        key = "F3"; break;
    case SDLK_F4:        key = "F4"; break;
    case SDLK_F5:        key = "F5"; break;
    case SDLK_F6:        key = "F6"; break;
    case SDLK_F7:        key = "F7"; break;
    case SDLK_F8:        key = "F8"; break;
    case SDLK_F9:        key = "F9"; break;
    case SDLK_F10:       key = "F10"; break;
    case SDLK_F11:       key = "F11"; break;
    case SDLK_F12:       key = "F12"; break;
    case SDLK_SPACE:     key = "Space"; break;
    case SDLK_LESS:      key = "lt"; break;
    case SDLK_BACKSLASH: key = "Bslash"; break;
    default: break;
    }

    if (key.empty() && (mod & SDL_KMOD_CTRL)) {
        if (keycode >= SDLK_A && keycode <= SDLK_Z) {
            key = std::string(1, (char)('a' + (keycode - SDLK_A)));
        } else if (keycode >= SDLK_0 && keycode <= SDLK_9) {
            key = std::string(1, (char)('0' + (keycode - SDLK_0)));
        } else if (keycode == SDLK_LEFTBRACKET) {
            key = "[";
        } else if (keycode == SDLK_RIGHTBRACKET) {
            key = "]";
        }
    }

    if (key.empty()) return "";

    std::string result = "<";
    if (mod & SDL_KMOD_SHIFT) result += "S-";
    if (mod & SDL_KMOD_CTRL)  result += "C-";
    if (mod & SDL_KMOD_ALT)   result += "A-";
    result += key + ">";

    return result;
}

void NvimInput::on_key(const KeyEvent& event) {
    if (!event.pressed) return;

    uint16_t mod = event.mod;
    int keycode = event.keycode;

    std::string translated = translate_key(keycode, mod);

    if (!translated.empty()) {
        suppress_next_text_ = (mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0;
        send_input(translated);
    } else {
        suppress_next_text_ = false;
    }
}

void NvimInput::on_text_input(const TextInputEvent& event) {
    if (suppress_next_text_) {
        suppress_next_text_ = false;
        return;
    }

    std::string text = event.text;
    if (text == "<") {
        send_input("<lt>");
    } else {
        send_input(text);
    }
}

void NvimInput::on_mouse_button(const MouseButtonEvent& event) {
    int grid_col = event.x / cell_w_;
    int grid_row = event.y / cell_h_;

    std::string button;
    switch (event.button) {
    case SDL_BUTTON_LEFT:   button = "left"; break;
    case SDL_BUTTON_RIGHT:  button = "right"; break;
    case SDL_BUTTON_MIDDLE: button = "middle"; break;
    default: return;
    }

    if (event.pressed) mouse_pressed_ = true;
    else mouse_pressed_ = false;

    std::string action = event.pressed ? "press" : "release";

    rpc_->notify("nvim_input_mouse", {
        NvimRpc::make_str(button),
        NvimRpc::make_str(action),
        NvimRpc::make_str(""),
        NvimRpc::make_int(0),
        NvimRpc::make_int(grid_row),
        NvimRpc::make_int(grid_col)
    });
}

void NvimInput::on_mouse_move(const MouseMoveEvent& event) {
    if (!mouse_pressed_) return;

    int grid_col = event.x / cell_w_;
    int grid_row = event.y / cell_h_;

    rpc_->notify("nvim_input_mouse", {
        NvimRpc::make_str("left"),
        NvimRpc::make_str("drag"),
        NvimRpc::make_str(""),
        NvimRpc::make_int(0),
        NvimRpc::make_int(grid_row),
        NvimRpc::make_int(grid_col)
    });
}

void NvimInput::on_mouse_wheel(const MouseWheelEvent& event) {
    int grid_col = event.x / cell_w_;
    int grid_row = event.y / cell_h_;

    std::string direction;
    if (event.dy > 0) direction = "up";
    else if (event.dy < 0) direction = "down";
    else if (event.dx > 0) direction = "right";
    else if (event.dx < 0) direction = "left";
    else return;

    rpc_->notify("nvim_input_mouse", {
        NvimRpc::make_str("wheel"),
        NvimRpc::make_str(direction),
        NvimRpc::make_str(""),
        NvimRpc::make_int(0),
        NvimRpc::make_int(grid_row),
        NvimRpc::make_int(grid_col)
    });
}

} // namespace spectre
