#include "input_dispatcher.h"

#include "gui_action_handler.h"
#include "host_manager.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <draxul/app_config.h>
#include <draxul/events.h>
#include <draxul/host.h>
#include <draxul/keybinding_parser.h>
#include <draxul/perf_timing.h>
#include <draxul/ui_panel.h>
#include <draxul/window.h>

namespace draxul
{

InputDispatcher::InputDispatcher(Deps deps)
    : deps_(std::move(deps))
{
}

void InputDispatcher::set_chord_indicator_fade_ms(int fade_ms)
{
    chord_indicator_fade_ms_ = std::max(100, fade_ms);
}

void InputDispatcher::start_indicator_fade(std::chrono::steady_clock::time_point now)
{
    const auto fade_duration = std::chrono::milliseconds(chord_indicator_fade_ms_);
    fade_started_at_ = now;
    fade_ends_at_ = now + fade_duration;
}

// Returns the host that should receive a mouse event at (px, py).
// px, py are SDL logical coordinates. PaneDescriptor boundaries are stored in
// physical pixels, so we scale before hit-testing.
IHost* InputDispatcher::host_for_mouse_pos(int px, int py)
{
    PERF_MEASURE();
    HostManager* hm = deps_.host_manager ? deps_.host_manager() : nullptr;
    if (hm)
    {
        const int phys_x = deps_.pixel_scale.to_physical(px);
        const int phys_y = deps_.pixel_scale.to_physical(py);
        IHost* target = hm->host_at_point(phys_x, phys_y);
        if (target)
        {
            deps_.host = target;
            return target;
        }
    }
    return deps_.host;
}

static bool is_modifier_only_key(const KeyEvent& event)
{
    switch (event.keycode)
    {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
    case SDLK_LCTRL:
    case SDLK_RCTRL:
    case SDLK_LALT:
    case SDLK_RALT:
    case SDLK_LGUI:
    case SDLK_RGUI:
        return true;
    default:
        return false;
    }
}

std::optional<int> digit_from_keycode(int32_t keycode)
{
    switch (keycode)
    {
    case SDLK_1:
        return 1;
    case SDLK_2:
        return 2;
    case SDLK_3:
        return 3;
    case SDLK_4:
        return 4;
    case SDLK_5:
        return 5;
    case SDLK_6:
        return 6;
    case SDLK_7:
        return 7;
    case SDLK_8:
        return 8;
    case SDLK_9:
        return 9;
    default:
        return std::nullopt;
    }
}

ModifierFlags normalize_modifiers(ModifierFlags mod)
{
    ModifierFlags result = kModNone;
    if (mod & kModShift)
        result |= kModShift;
    if (mod & kModCtrl)
        result |= kModCtrl;
    if (mod & kModAlt)
        result |= kModAlt;
    if (mod & kModSuper)
        result |= kModSuper;
    return result;
}

std::string chord_step_display(const KeyEvent& event)
{
    return format_gui_keybinding_combo(event.keycode, event.mod);
}

template <typename Deps>
void request_imgui_frame_if_needed(const Deps& deps)
{
    if (deps.ui_panel->wants_keyboard() && deps.request_frame)
        deps.request_frame();
}

void InputDispatcher::on_key_event(const KeyEvent& event)
{
    PERF_MEASURE();
    const auto now = std::chrono::steady_clock::now();

    // Overlay host (e.g. command palette) intercepts all input when active.
    if (deps_.overlay_host)
    {
        if (IHost* overlay = deps_.overlay_host())
        {
            overlay->on_key(event);
            deps_.ui_panel->on_key(event);
            request_imgui_frame_if_needed(deps_);
            return;
        }
    }

    if (event.pressed)
    {
        if (prefix_active_)
        {
            // Modifier-only keys (Shift, Ctrl, Alt, Super) don't cancel prefix mode —
            // the chord's second key may itself require a modifier (e.g. Shift+\ for |).
            if (is_modifier_only_key(event))
            {
                deps_.ui_panel->on_key(event);
                request_imgui_frame_if_needed(deps_);
                return;
            }
            // We consumed the prefix key; now look for a chord match.
            const std::string prefix_text = indicator_text_;
            prefix_active_ = false;
            prefix_started_at_.reset();
            if (pane_select_active_)
            {
                pane_select_active_ = false;
                const std::string final_text = prefix_text + " " + chord_step_display(event);
                if (auto digit = digit_from_keycode(event.keycode);
                    digit.has_value() && normalize_modifiers(event.mod) == kModNone && deps_.activate_pane)
                {
                    suppress_next_text_input_ = true;
                    indicator_text_ = final_text;
                    start_indicator_fade(now);
                    deps_.activate_pane(*digit);
                    if (deps_.request_frame)
                        deps_.request_frame();
                    return;
                }

                indicator_text_ = final_text;
                start_indicator_fade(now);
                if (deps_.request_frame)
                    deps_.request_frame();
                // No pane matched — fall through and forward to host normally.
            }
            else if (event.keycode == SDLK_0 && normalize_modifiers(event.mod) == kModNone && deps_.activate_pane)
            {
                pane_select_active_ = true;
                prefix_active_ = true;
                prefix_started_at_ = now;
                indicator_text_ = prefix_text + " 0";
                fade_started_at_.reset();
                fade_ends_at_.reset();
                suppress_next_text_input_ = true;
                if (deps_.request_frame)
                    deps_.request_frame();
                return;
            }
            if (deps_.keybindings && deps_.gui_action_handler)
            {
                for (const auto& binding : *deps_.keybindings)
                {
                    if (binding.prefix_key != 0 && gui_keybinding_matches(binding, event))
                    {
                        suppress_next_text_input_ = true;
                        indicator_text_ = prefix_text + " " + chord_step_display(event);
                        start_indicator_fade(now);
                        deps_.gui_action_handler->execute(binding.action);
                        if (deps_.request_frame)
                            deps_.request_frame();
                        return; // chord consumed — do not forward to host
                    }
                }
            }
            indicator_text_ = prefix_text + " " + chord_step_display(event);
            start_indicator_fade(now);
            if (deps_.request_frame)
                deps_.request_frame();
            // No chord matched — fall through and forward to host normally.
        }
        else
        {
            // Check if this key activates a chord prefix.
            if (deps_.keybindings)
            {
                for (const auto& binding : *deps_.keybindings)
                {
                    if (gui_prefix_matches(binding, event))
                    {
                        prefix_active_ = true;
                        pane_select_active_ = false;
                        prefix_started_at_ = now;
                        indicator_text_ = format_gui_keybinding_combo(binding.prefix_key, binding.prefix_modifiers);
                        fade_started_at_.reset();
                        fade_ends_at_.reset();
                        if (deps_.request_frame)
                            deps_.request_frame();
                        return; // swallow the prefix key
                    }
                }
            }
            // Check single-key (non-chord) GUI bindings.
            if (auto action = gui_action_for_key_event(event);
                action && deps_.gui_action_handler && deps_.gui_action_handler->execute(*action))
                return;
        }
    }
    else
    {
        // Key release — cancel prefix mode if the prefix key itself was released
        // (handles the case where the user holds the prefix key without pressing a chord key).
        // We don't cancel on every key-up, only on explicit Escape-like cancellation via
        // a future policy. For now just keep prefix_active_ until the next key-down.
    }
    deps_.ui_panel->on_key(event);
    request_imgui_frame_if_needed(deps_);
    if (!deps_.ui_panel->wants_keyboard() && deps_.host)
        deps_.host->on_key(event);
}

void InputDispatcher::on_mouse_button_event(const MouseButtonEvent& event)
{
    PERF_MEASURE();

    // Overlay host consumes mouse button events — don't leak clicks to the
    // underlying host while e.g. the command palette is open.
    if (deps_.overlay_host && deps_.overlay_host())
        return;

    // Tab bar click — check before anything else.
    if (event.pressed && deps_.hit_test_tab && deps_.activate_tab)
    {
        const int phys_x = deps_.pixel_scale.to_physical(event.pos.x);
        const int phys_y = deps_.pixel_scale.to_physical(event.pos.y);
        if (int tab_index = deps_.hit_test_tab(phys_x, phys_y); tab_index > 0)
        {
            deps_.activate_tab(tab_index);
            return;
        }
    }

    deps_.ui_panel->on_mouse_button(event);
    IHost* target = host_for_mouse_pos(event.pos.x, event.pos.y);
    if (deps_.ui_panel->wants_mouse() && deps_.request_frame)
        deps_.request_frame();
    if (deps_.ui_panel->wants_mouse())
        return;
    if (deps_.ui_panel->layout().contains_panel_point(deps_.pixel_scale.to_physical(event.pos.x), deps_.pixel_scale.to_physical(event.pos.y)))
    {
        return;
    }
    if (target)
    {
        // Hosts store viewports and cell sizes in physical pixels; translate
        // the SDL logical coordinates to physical before forwarding.
        MouseButtonEvent phys = event;
        phys.pos.x = deps_.pixel_scale.to_physical(event.pos.x);
        phys.pos.y = deps_.pixel_scale.to_physical(event.pos.y);
        target->on_mouse_button(phys);
    }
}

void InputDispatcher::on_mouse_move_event(const MouseMoveEvent& event)
{
    PERF_MEASURE();

    // Overlay host consumes mouse move events — don't let hover/drag reach
    // the underlying host while an overlay (e.g. command palette) is active.
    if (deps_.overlay_host && deps_.overlay_host())
        return;

    deps_.ui_panel->on_mouse_move(event);
    IHost* target = host_for_mouse_pos(event.pos.x, event.pos.y);
    if (deps_.ui_panel->wants_mouse() && deps_.request_frame)
        deps_.request_frame();
    if (deps_.ui_panel->wants_mouse())
        return;
    if (deps_.ui_panel->layout().contains_panel_point(deps_.pixel_scale.to_physical(event.pos.x), deps_.pixel_scale.to_physical(event.pos.y)))
    {
        return;
    }
    if (target)
    {
        MouseMoveEvent phys = event;
        phys.pos.x = deps_.pixel_scale.to_physical(event.pos.x);
        phys.pos.y = deps_.pixel_scale.to_physical(event.pos.y);
        phys.delta *= deps_.pixel_scale.value();
        target->on_mouse_move(phys);
    }
}

void InputDispatcher::on_mouse_wheel_event(const MouseWheelEvent& event)
{
    PERF_MEASURE();

    // Overlay host consumes scroll events — don't scroll the underlying
    // terminal while e.g. the command palette is open.
    if (deps_.overlay_host && deps_.overlay_host())
        return;

    deps_.ui_panel->on_mouse_wheel(event);
    IHost* wheel_host = host_for_mouse_pos(event.pos.x, event.pos.y);
    if (deps_.ui_panel->wants_mouse() && deps_.request_frame)
        deps_.request_frame();
    if (deps_.ui_panel->wants_mouse())
        return;
    if (deps_.ui_panel->layout().contains_panel_point(deps_.pixel_scale.to_physical(event.pos.x), deps_.pixel_scale.to_physical(event.pos.y)))
        return;
    if (!wheel_host)
        return;

    // Build a physical-coordinate version of the event for forwarding to the host.
    MouseWheelEvent phys_event = event;
    phys_event.pos.x = deps_.pixel_scale.to_physical(event.pos.x);
    phys_event.pos.y = deps_.pixel_scale.to_physical(event.pos.y);

    if (deps_.smooth_scroll && event.delta.y != 0.0f)
    {
        had_scroll_event_ = true;
        pending_scroll_y_ += event.delta.y * deps_.scroll_speed;
        const float sign = pending_scroll_y_ > 0.0f ? 1.0f : -1.0f;
        const auto steps = static_cast<int>(std::abs(pending_scroll_y_));
        for (int i = 0; i < steps; ++i)
        {
            MouseWheelEvent step = phys_event;
            step.delta.y = sign;
            wheel_host->on_mouse_wheel(step);
        }
        pending_scroll_y_ = std::fmod(pending_scroll_y_, 1.0f);
        if (deps_.request_frame)
            deps_.request_frame();
    }
    else
    {
        pending_scroll_y_ = 0.0f;
        wheel_host->on_mouse_wheel(phys_event);
    }
}

void InputDispatcher::set_host(IHost* host)
{
    if (deps_.host && deps_.host != host)
        deps_.host->on_focus_lost();
    deps_.host = host;
}

void InputDispatcher::connect(IWindow& window)
{
    PERF_MEASURE();
    window.on_key = [this](const KeyEvent& e) { on_key_event(e); };

    window.on_text_input = [this](const TextInputEvent& event) {
        if (suppress_next_text_input_)
        {
            suppress_next_text_input_ = false;
            return;
        }
        // Overlay host consumes text input when active.
        if (deps_.overlay_host)
        {
            if (IHost* overlay = deps_.overlay_host())
            {
                overlay->on_text_input(event);
                request_imgui_frame_if_needed(deps_);
                return;
            }
        }
        deps_.ui_panel->on_text_input(event);
        request_imgui_frame_if_needed(deps_);
        if (!deps_.ui_panel->wants_keyboard() && deps_.host)
            deps_.host->on_text_input(event);
    };

    window.on_text_editing = [this](const TextEditingEvent& event) {
        // Overlay host consumes IME composition events.
        if (deps_.overlay_host && deps_.overlay_host())
            return;
        request_imgui_frame_if_needed(deps_);
        if (deps_.host)
            deps_.host->on_text_editing(event);
    };

    window.on_mouse_button = [this](const MouseButtonEvent& e) { on_mouse_button_event(e); };
    window.on_mouse_move = [this](const MouseMoveEvent& e) { on_mouse_move_event(e); };
    window.on_mouse_wheel = [this](const MouseWheelEvent& e) { on_mouse_wheel_event(e); };

    window.on_resize = [this](const WindowResizeEvent& event) {
        if (deps_.on_resize)
            deps_.on_resize(event.size.x, event.size.y);
    };

    window.on_display_scale_changed = [this](const DisplayScaleEvent& event) {
        if (deps_.on_display_scale_changed)
            deps_.on_display_scale_changed(event.display_ppi);
    };

    window.on_drop_file = [this](std::string_view path) {
        if (deps_.host)
            deps_.host->dispatch_action(std::string("open_file:") + std::string(path));
    };
}

InputDispatcher::ChordIndicatorState InputDispatcher::chord_indicator_state(
    std::chrono::steady_clock::time_point now) const
{
    ChordIndicatorState state;
    state.text = indicator_text_;
    if (state.text.empty())
        return state;

    if (fade_started_at_ && fade_ends_at_)
    {
        const auto total = std::chrono::duration_cast<std::chrono::milliseconds>(*fade_ends_at_ - *fade_started_at_);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - *fade_started_at_);
        if (total.count() <= 0)
        {
            state.alpha = 0.0f;
            return state;
        }
        const float t = std::clamp(static_cast<float>(elapsed.count()) / static_cast<float>(total.count()), 0.0f, 1.0f);
        state.alpha = 1.0f - t;
        return state;
    }

    state.alpha = 1.0f;
    return state;
}

bool InputDispatcher::update(std::chrono::steady_clock::time_point now, int chord_timeout_ms)
{
    bool changed = false;

    if (prefix_active_ && prefix_started_at_)
    {
        const auto timeout = std::chrono::milliseconds(std::max(100, chord_timeout_ms));
        if (now - *prefix_started_at_ >= timeout)
        {
            prefix_active_ = false;
            pane_select_active_ = false;
            prefix_started_at_.reset();
            start_indicator_fade(now);
            changed = true;
        }
    }

    if (fade_ends_at_)
    {
        if (now >= *fade_ends_at_)
        {
            indicator_text_.clear();
            fade_started_at_.reset();
            fade_ends_at_.reset();
            changed = true;
        }
        else if (deps_.request_frame)
        {
            deps_.request_frame();
        }
    }

    return changed;
}

std::optional<std::string_view> InputDispatcher::gui_action_for_key_event(const KeyEvent& event) const
{
    PERF_MEASURE();
    if (!deps_.keybindings)
        return std::nullopt;
    for (const auto& binding : *deps_.keybindings)
    {
        // Skip chord bindings — those are only dispatched from the prefix state machine above.
        if (binding.prefix_key != 0)
            continue;
        if (gui_keybinding_matches(binding, event))
            return binding.action;
    }
    return std::nullopt;
}

} // namespace draxul
