#include <spectre/sdl_window.h>
#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#include <CoreGraphics/CoreGraphics.h>
#else
#include <SDL3/SDL_vulkan.h>
#endif
#include <cmath>

namespace spectre {

bool SdlWindow::initialize(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

#ifdef __APPLE__
    Uint64 window_flags = SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    Uint64 window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
#endif

    window_ = SDL_CreateWindow(
        title.c_str(),
        width, height,
        window_flags
    );

    if (!window_) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    // Enable text input events (required in SDL3)
    SDL_StartTextInput(window_);

    return true;
}

void SdlWindow::shutdown() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

bool SdlWindow::poll_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            return false;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (on_resize) {
                // Use pixel dimensions (not points) for correct Retina/HiDPI rendering
                int pw, ph;
                SDL_GetWindowSizeInPixels(window_, &pw, &ph);
                on_resize({ pw, ph });
            }
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (on_key) {
                on_key({
                    (int)event.key.scancode,
                    (int)event.key.key,
                    event.key.mod,
                    event.type == SDL_EVENT_KEY_DOWN
                });
            }
            break;

        case SDL_EVENT_TEXT_INPUT:
            if (on_text_input) {
                on_text_input({ event.text.text });
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (on_mouse_button) {
                on_mouse_button({
                    (int)event.button.button,
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
                    (int)event.button.x,
                    (int)event.button.y
                });
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (on_mouse_move) {
                on_mouse_move({ (int)event.motion.x, (int)event.motion.y });
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            if (on_mouse_wheel) {
                on_mouse_wheel({
                    event.wheel.x, event.wheel.y,
                    (int)event.wheel.mouse_x, (int)event.wheel.mouse_y
                });
            }
            break;
        }
    }
    return true;
}

std::pair<int,int> SdlWindow::size_pixels() const {
    int w = 0, h = 0;
    if (window_) {
        SDL_GetWindowSizeInPixels(window_, &w, &h);
    }
    return { w, h };
}

float SdlWindow::display_ppi() const {
#ifdef __APPLE__
    // Get actual physical PPI from CoreGraphics
    CGDirectDisplayID displayID = CGMainDisplayID();
    CGSize physicalSize = CGDisplayScreenSize(displayID); // millimeters
    size_t pixelWidth = CGDisplayPixelsWide(displayID);
    if (physicalSize.width > 0) {
        return (float)(pixelWidth / (physicalSize.width / 25.4));
    }
#else
    // Windows: use GetDpiForWindow via SDL's content scale
    // SDL_GetWindowDisplayScale accounts for system DPI settings
    if (window_) {
        float scale = SDL_GetWindowDisplayScale(window_);
        if (scale > 0.0f) {
            return 96.0f * scale; // Windows base DPI is 96
        }
    }
#endif
    return 96.0f; // fallback
}

} // namespace spectre
