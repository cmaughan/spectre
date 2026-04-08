#pragma once

#include "split_tree.h"

#include <algorithm>
#include <chrono>
#include <draxul/pixel_scale.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

struct GuiKeybinding;
class GuiActionHandler;
class UiPanel;
class IHost;
class IWindow;
class HostManager;
class SdlWindow;
struct KeyEvent;
struct MouseButtonEvent;
struct MouseMoveEvent;
struct MouseWheelEvent;

// Wires SDL window input callbacks and routes events:
//   - Key events: checks GUI keybindings first (via GuiActionHandler), then forwards to
//     UiPanel/host
//   - Mouse events: forwards to UiPanel when inside the panel, otherwise to host
//   - Text-editing events: forwarded directly to the host
//
// App creates one InputDispatcher after all subsystems are ready and calls connect() to
// install it as the window's event handlers.
class InputDispatcher
{
public:
    struct ChordIndicatorState
    {
        std::string text;
        float alpha = 0.0f;

        [[nodiscard]] bool visible() const
        {
            return !text.empty() && alpha > 0.0f;
        }
    };

    struct Deps
    {
        const std::vector<GuiKeybinding>* keybindings = nullptr;
        GuiActionHandler* gui_action_handler = nullptr;
        // Window used for cursor changes and pixel size queries during divider drag.
        IWindow* window = nullptr;
        // Returns the active overlay host (e.g. command palette) when it should
        // intercept all input, or null when no overlay is active.
        std::function<IHost*()> overlay_host;
        UiPanel* ui_panel = nullptr;
        IHost* host = nullptr;
        // Multi-pane: if set, mouse events are hit-tested via HostManager's SplitTree.
        // Function-based so it always returns the active workspace's HostManager.
        std::function<HostManager*()> host_manager;
        bool smooth_scroll = false;
        float scroll_speed = 1.0f;
        // Ratio of physical pixels to logical pixels (1.0 on non-HiDPI, 2.0 on Retina).
        // Used to convert SDL logical mouse coordinates to physical pixels for hit-testing
        // pane descriptors (which are stored in physical pixels) and for forwarding to hosts.
        PixelScale pixel_scale;

        std::function<void()> request_frame;
        std::function<void(int, int)> on_resize;
        std::function<void(float)> on_display_scale_changed;
        // Tab bar click: returns 1-based tab index if (px, py) hits a tab, else 0.
        std::function<int(int, int)> hit_test_tab;
        // Pane status pill click: returns the leaf id if (px, py) hits a pill,
        // else kInvalidLeaf. Used to drive double-click pane rename and to
        // suppress drag selection from spilling into the underlying host.
        std::function<LeafId(int, int)> hit_test_pane_pill;
        // Begin a rename session for the pane identified by leaf id.
        std::function<void(LeafId)> begin_pane_rename;
        // Height of the top chrome (tab bar) in physical pixels. Mouse events
        // with phys_y < this height never reach the underlying host so the
        // tab bar cannot start drag selections in the terminal beneath.
        std::function<int()> tab_bar_height_phys;
        // Cell size in physical pixels. Used to quantize divider drag in
        // cell-aligned (tmux-style) steps so terminal grids don't visually
        // jitter when crossing pixel-row thresholds.
        std::function<std::pair<int, int>()> cell_size_phys;
        // Activate tab by 1-based index.
        std::function<void(int)> activate_tab;
        // Activate pane by 1-based visual index within the active workspace.
        std::function<void(int)> activate_pane;
        // ----- Inline tab rename (WI 128) -------------------------------
        // Begin a rename session for the tab at 1-based index.
        std::function<void(int)> begin_tab_rename;
        // True while ChromeHost has a rename session in progress.
        std::function<bool()> is_editing_tab;
        // Forward a typed UTF-8 chunk to the rename buffer. Returns true if
        // the rename layer consumed it.
        std::function<bool(const std::string&)> rename_text_input;
        // Forward an SDL keycode to the rename layer. Returns true if
        // consumed (Enter/Escape/Backspace/etc.).
        std::function<bool(int)> rename_key;
        // Commit the active rename buffer (used on click-outside).
        std::function<void()> commit_tab_rename;
    };

    explicit InputDispatcher(Deps deps);

    // Installs this dispatcher's lambdas as the window's event callbacks.
    void connect(IWindow& window);

    // Updates the host pointer (used when focus changes between panes).
    void set_host(IHost* host);

    // Updates the pixel scale (called when the display DPI changes).
    void set_pixel_scale(PixelScale scale)
    {
        deps_.pixel_scale = scale;
    }

    void set_scroll_config(bool smooth_scroll, float scroll_speed)
    {
        deps_.smooth_scroll = smooth_scroll;
        deps_.scroll_speed = scroll_speed;
    }

    void set_chord_indicator_fade_ms(int fade_ms);

    // Exposed for testing — checks if the key event matches any GUI keybinding.
    std::optional<std::string_view> gui_action_for_key_event(const KeyEvent& event) const;
    [[nodiscard]] ChordIndicatorState chord_indicator_state(std::chrono::steady_clock::time_point now) const;
    bool update(std::chrono::steady_clock::time_point now, int chord_timeout_ms);

    // The fractional portion of accumulated scroll (in cells) not yet committed to the host.
    // Only valid if had_scroll_event() is true; returns 0 when no wheel event arrived this frame.
    // Positive = pending upward scroll; negative = pending downward scroll.
    float scroll_fraction() const
    {
        return had_scroll_event_ ? pending_scroll_y_ : 0.0f;
    }

    // True if at least one mouse-wheel event was processed since the last clear_scroll_event().
    bool had_scroll_event() const
    {
        return had_scroll_event_;
    }

    // Called once per frame (after applying the scroll offset) to clear the per-frame flag.
    // The fractional accumulator (pending_scroll_y_) is intentionally preserved so that
    // slow trackpad gestures can accumulate across events; only the visual-offset flag is reset.
    void clear_scroll_event()
    {
        had_scroll_event_ = false;
    }

private:
    void start_indicator_fade(std::chrono::steady_clock::time_point now);
    void on_key_event(const KeyEvent& event);
    void on_mouse_button_event(const MouseButtonEvent& event);
    void on_mouse_move_event(const MouseMoveEvent& event);
    void on_mouse_wheel_event(const MouseWheelEvent& event);
    // Returns the host that should receive mouse events at (px, py).
    IHost* host_for_mouse_pos(int px, int py);
    // Update mouse cursor based on whether (phys_x, phys_y) is over a divider.
    // Returns true if a divider is under the point. Skipped while dragging.
    bool update_cursor_for_divider(int phys_x, int phys_y);

    Deps deps_;
    // Divider drag state.
    int drag_divider_id_ = -1; // kInvalidDivider
    int active_mouse_cursor_ = 0; // 0=default, 1=ew, 2=ns — avoids redundant calls
    float pending_scroll_y_ = 0.0f;
    bool had_scroll_event_ = false;
    // Chord (tmux-style prefix) state: true when a prefix key has been consumed and
    // we are waiting for the second key of a chord binding.
    bool prefix_active_ = false;
    // Set when a chord action fires; causes the immediately following text-input event
    // (SDL_EVENT_TEXT_INPUT for the chord's second key) to be suppressed.
    bool suppress_next_text_input_ = false;
    bool pane_select_active_ = false;
    std::string indicator_text_;
    std::optional<std::chrono::steady_clock::time_point> prefix_started_at_;
    std::optional<std::chrono::steady_clock::time_point> fade_started_at_;
    std::optional<std::chrono::steady_clock::time_point> fade_ends_at_;
    int chord_indicator_fade_ms_ = 2500;
};

} // namespace draxul
