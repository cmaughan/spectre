#include <spectre/sdl_window.h>
#include <SDL3/SDL.h>
#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#include <CoreGraphics/CoreGraphics.h>
#elif defined(_WIN32)
#include <SDL3/SDL_vulkan.h>
#include <windows.h>
#include <shellscalingapi.h>
#else
#include <SDL3/SDL_vulkan.h>
#endif
#include <cmath>
#include <cstdio>

namespace spectre {

#ifdef _WIN32
static void log_display_info(SDL_Window* window) {
    fprintf(stderr, "\n=== Display / DPI Diagnostics ===\n");

    // SDL window sizes
    int lw = 0, lh = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(window, &lw, &lh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    float sdl_scale = SDL_GetWindowDisplayScale(window);
    fprintf(stderr, "  SDL logical size       : %d x %d\n", lw, lh);
    fprintf(stderr, "  SDL pixel size         : %d x %d\n", pw, ph);
    fprintf(stderr, "  SDL display scale      : %.3f  (= pixel/logical ratio)\n", sdl_scale);
    fprintf(stderr, "  SDL effective DPI      : %.1f  (96 * scale)\n", 96.0f * sdl_scale);

    // Win32 HWND-based queries
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    if (hwnd) {
        // Per-monitor DPI variants
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        UINT eff_x = 0, eff_y = 0, ang_x = 0, ang_y = 0, raw_x = 0, raw_y = 0;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &eff_x, &eff_y);
        GetDpiForMonitor(monitor, MDT_ANGULAR_DPI,  &ang_x, &ang_y);
        GetDpiForMonitor(monitor, MDT_RAW_DPI,      &raw_x, &raw_y);
        fprintf(stderr, "  MDT_EFFECTIVE_DPI      : %u x %u\n", eff_x, eff_y);
        fprintf(stderr, "  MDT_ANGULAR_DPI        : %u x %u\n", ang_x, ang_y);
        fprintf(stderr, "  MDT_RAW_DPI            : %u x %u\n", raw_x, raw_y);

        // Monitor logical/physical rect
        MONITORINFOEX mi = {};
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(monitor, &mi);
        int mon_lw = mi.rcMonitor.right - mi.rcMonitor.left;
        int mon_lh = mi.rcMonitor.bottom - mi.rcMonitor.top;
        fprintf(stderr, "  Monitor logical rect   : %d x %d  (%s)\n",
            mon_lw, mon_lh,
            (mi.dwFlags & MONITORINFOF_PRIMARY) ? "primary" : "non-primary");

        // Physical pixel resolution via DEVMODE
        DEVMODE dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettings(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
            fprintf(stderr, "  Monitor device pixels  : %lu x %lu\n",
                dm.dmPelsWidth, dm.dmPelsHeight);
            // Compute PPI from physical pixels + physical mm (via GetDeviceCaps)
            HDC hdc = CreateDC(NULL, mi.szDevice, NULL, NULL);
            if (hdc) {
                int mm_w = GetDeviceCaps(hdc, HORZSIZE);
                int mm_h = GetDeviceCaps(hdc, VERTSIZE);
                int log_px_w = GetDeviceCaps(hdc, HORZRES);
                int log_px_h = GetDeviceCaps(hdc, VERTRES);
                int desk_px_w = GetDeviceCaps(hdc, DESKTOPHORZRES);
                int desk_px_h = GetDeviceCaps(hdc, DESKTOPVERTRES);
                int logpx_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
                DeleteDC(hdc);
                fprintf(stderr, "  GetDeviceCaps HORZSIZE / VERTSIZE   : %d x %d mm\n", mm_w, mm_h);
                fprintf(stderr, "  GetDeviceCaps HORZRES / VERTRES     : %d x %d  (logical px)\n", log_px_w, log_px_h);
                fprintf(stderr, "  GetDeviceCaps DESKTOPHORZRES/VERT   : %d x %d  (physical px)\n", desk_px_w, desk_px_h);
                fprintf(stderr, "  GetDeviceCaps LOGPIXELSX            : %d\n", logpx_dpi);
                if (mm_w > 0) {
                    float phys_ppi = dm.dmPelsWidth / (mm_w / 25.4f);
                    fprintf(stderr, "  Computed physical PPI (DEVMODE/mm)  : %.1f\n", phys_ppi);
                }
            }
        }
    }
    fprintf(stderr, "=================================\n\n");
}
#endif

bool SdlWindow::initialize(const std::string& title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

#ifdef __APPLE__
    Uint64 window_flags = SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
    Uint64 window_flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    // On Windows, SDL uses per-monitor DPI v2 awareness: window dimensions are in
    // physical pixels, not logical pixels. Scale up so the window appears at the
    // intended logical size on the display.
    {
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        float scale = display ? SDL_GetDisplayContentScale(display) : 1.0f;
        if (scale > 1.0f) {
            width  = (int)std::round(width  * scale);
            height = (int)std::round(height * scale);
        }
    }
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

#ifdef _WIN32
    log_display_info(window_);
#endif

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
#elif defined(_WIN32)
    // Windows: get actual physical DPI from monitor hardware (EDID)
    if (window_) {
        HWND hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
        if (hwnd) {
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            UINT dpi_x = 0, dpi_y = 0;
            if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_RAW_DPI, &dpi_x, &dpi_y)) && dpi_x > 0) {
                return (float)dpi_x;
            }
        }
    }
#endif
    return 96.0f; // fallback
}

} // namespace spectre
