#include "toast_host.h"

#include <algorithm>
#include <cstring>
#include <draxul/text_service.h>

namespace draxul
{

void ToastHost::push(gui::ToastLevel level, std::string message, float duration_s)
{
    std::lock_guard lock(pending_mutex_);
    pending_.push_back({ level, std::move(message), duration_s });
}

bool ToastHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    renderer_ = context.grid_renderer;
    text_service_ = context.text_service;
    if (!renderer_)
        return false;

    set_viewport(context.initial_viewport);
    handle_ = renderer_->create_grid_handle();

    // Toast grid covers the full window but only emits cells for the small
    // toast region; the rest of the grid must stay fully transparent so the
    // hosts beneath show through.
    handle_->set_default_background({ 0.0f, 0.0f, 0.0f, 0.0f });

    PaneDescriptor desc;
    desc.pixel_size = { pixel_w_, pixel_h_ };
    handle_->set_viewport(desc);

    last_tick_ = std::chrono::steady_clock::now();
    return true;
}

void ToastHost::shutdown()
{
    handle_.reset();
}

bool ToastHost::is_running() const
{
    return true;
}

std::string ToastHost::init_error() const
{
    return {};
}

void ToastHost::set_viewport(const HostViewport& viewport)
{
    pixel_w_ = std::max(1, viewport.pixel_size.x);
    pixel_h_ = std::max(1, viewport.pixel_size.y);
    if (handle_)
    {
        PaneDescriptor desc;
        desc.pixel_size = { pixel_w_, pixel_h_ };
        handle_->set_viewport(desc);
    }
}

void ToastHost::pump()
{
    // Drain pending toasts from other threads.
    bool had_active = !active_.empty();
    {
        std::lock_guard lock(pending_mutex_);
        for (auto& p : pending_)
        {
            active_.push_back({
                p.level,
                std::move(p.message),
                p.duration_s,
                p.duration_s,
            });
        }
        pending_.clear();
    }

    if (active_.empty())
        return;

    // Tick timers. If no toasts were active before this pump, reset the tick
    // baseline so we don't subtract the entire idle interval (which would
    // immediately expire freshly pushed toasts).
    const auto now = std::chrono::steady_clock::now();
    if (!had_active)
        last_tick_ = now;
    const float dt = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;

    for (auto& entry : active_)
        entry.remaining_s -= dt;

    // Remove expired toasts.
    std::erase_if(active_, [](const gui::ToastEntry& e) { return e.remaining_s <= 0.0f; });

    // Schedule a follow-up frame while toasts are visible (for fade animation).
    if (!active_.empty() && callbacks_)
        callbacks_->request_frame();

    refresh();
}

void ToastHost::refresh()
{
    if (!handle_ || !renderer_ || !text_service_)
        return;

    const auto [cw, ch] = renderer_->cell_size_pixels();
    const int grid_cols = cw > 0 ? pixel_w_ / cw : 0;
    const int grid_rows = ch > 0 ? pixel_h_ / ch : 0;
    if (grid_cols <= 0 || grid_rows <= 0)
        return;

    handle_->set_grid_size(grid_cols, grid_rows);
    handle_->set_cursor(-1, -1, CursorStyle{});

    gui::ToastViewState vs;
    vs.grid_cols = grid_cols;
    vs.grid_rows = grid_rows;
    vs.entries = active_;

    auto cells = gui::render_toasts(vs, *text_service_);
    handle_->update_cells(cells);

    flush_atlas_if_dirty();
}

void ToastHost::draw(IFrameContext& frame)
{
    if (handle_ && !active_.empty())
        frame.draw_grid_handle(*handle_);
    frame.flush_submit_chunk();
}

std::optional<std::chrono::steady_clock::time_point> ToastHost::next_deadline() const
{
    if (active_.empty())
        return std::nullopt;
    // Request a frame every ~33ms for smooth fade animation.
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(33);
}

void ToastHost::on_key(const KeyEvent&) {}
void ToastHost::on_text_input(const TextInputEvent&) {}

bool ToastHost::dispatch_action(std::string_view)
{
    return false;
}

void ToastHost::request_close() {}

Color ToastHost::default_background() const
{
    return { 0.0f, 0.0f, 0.0f, 0.0f };
}

HostRuntimeState ToastHost::runtime_state() const
{
    return { .content_ready = true };
}

HostDebugState ToastHost::debug_state() const
{
    return { .name = "Toast" };
}

void ToastHost::flush_atlas_if_dirty()
{
    if (!text_service_->atlas_dirty())
        return;

    const auto dirty = text_service_->atlas_dirty_rect();
    if (dirty.size.x <= 0 || dirty.size.y <= 0)
        return;

    constexpr size_t kPixelSize = 4;
    const size_t row_bytes = static_cast<size_t>(dirty.size.x) * kPixelSize;
    std::vector<uint8_t> scratch(row_bytes * dirty.size.y);
    const uint8_t* atlas = text_service_->atlas_data();
    const int atlas_w = text_service_->atlas_width();
    for (int r = 0; r < dirty.size.y; ++r)
    {
        const uint8_t* src = atlas
            + (static_cast<size_t>(dirty.pos.y + r) * atlas_w + dirty.pos.x) * kPixelSize;
        std::memcpy(scratch.data() + static_cast<size_t>(r) * row_bytes, src, row_bytes);
    }
    renderer_->update_atlas_region(
        dirty.pos.x, dirty.pos.y, dirty.size.x, dirty.size.y, scratch.data());
    text_service_->clear_atlas_dirty();

    if (callbacks_ && !active_.empty())
        callbacks_->request_frame();
}

} // namespace draxul
