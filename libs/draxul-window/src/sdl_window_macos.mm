#import <Cocoa/Cocoa.h>
#include <SDL3/SDL.h>
#include <draxul/perf_timing.h>
#include <draxul/types.h>
#include <functional>

namespace draxul
{

void disable_press_and_hold_macos()
{
    // SDL3's Cocoa event handler registers ApplePressAndHoldEnabled = YES as a
    // default, which enables macOS's accent-picker popup when holding a key and
    // suppresses OS key-repeat events.  Terminal emulators need the opposite:
    // actual key repeat.  Explicitly setting the key (not just registering a
    // default) overrides SDL's registration and restores repeat behaviour.
    [[NSUserDefaults standardUserDefaults] setBool:NO forKey:@"ApplePressAndHoldEnabled"];
}

void apply_title_bar_color_macos(SDL_Window* window, Color color)
{
    PERF_MEASURE();
    NSWindow* ns_window = (__bridge NSWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!ns_window)
        return;

    ns_window.titlebarAppearsTransparent = YES;
    ns_window.backgroundColor = [NSColor colorWithRed:color.r green:color.g blue:color.b alpha:1.0];
}

// ---------------------------------------------------------------------------
// Dock reopen handler (Ghostty-style: click dock icon → show window)
// ---------------------------------------------------------------------------
// SDL3 installs its own NSApplicationDelegate, so we can't directly implement
// applicationShouldHandleReopen:hasVisibleWindows: without subclassing or
// swizzling SDL's delegate. Instead, we install a global NSAppleEventManager
// handler for the "reopen" Apple Event (kAEReopenApplication), which the
// system sends when the user clicks the Dock icon while no windows are visible.

namespace
{
std::function<void()>* g_reopen_callback = nullptr;
}

static OSErr handle_reopen_apple_event(const AppleEvent*, AppleEvent*, SRefCon)
{
    if (g_reopen_callback && *g_reopen_callback)
        (*g_reopen_callback)();
    return noErr;
}

void install_dock_reopen_handler(std::function<void()>* callback)
{
    g_reopen_callback = callback;
    [[NSAppleEventManager sharedAppleEventManager]
        setEventHandler:[NSApp delegate]
            andSelector:@selector(handleAppleEvent:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEReopenApplication];
    // Use the C API for a plain function handler instead.
    AEInstallEventHandler(kCoreEventClass, kAEReopenApplication,
        &handle_reopen_apple_event, 0, false);
}

} // namespace draxul
