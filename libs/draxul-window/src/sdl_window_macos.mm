#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <draxul/types.h>

namespace draxul
{

void apply_title_bar_color_macos(SDL_Window* window, Color color)
{
    NSWindow* ns_window = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!ns_window)
        return;

    ns_window.titlebarAppearsTransparent = YES;
    ns_window.backgroundColor = [NSColor colorWithRed:color.r green:color.g blue:color.b alpha:1.0];
}

} // namespace draxul
