#pragma once

#include <chrono>
#include <draxul/gui/toast_renderer.h>
#include <draxul/host.h>
#include <draxul/renderer.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace draxul
{

class TextService;

/// IHost implementation that renders auto-dismissing toast notifications as
/// grid overlay cells (same approach as CommandPaletteHost). Drawn as the
/// topmost layer so toasts appear above all other content.
///
/// Thread-safe: push() may be called from any thread; the host drains the
/// queue on the main thread during pump().
class ToastHost : public IHost
{
public:
    ToastHost() = default;

    /// Push a toast from any thread. The message will appear on the next frame.
    void push(gui::ToastLevel level, std::string message, float duration_s = 4.0f);

    // IHost interface
    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void shutdown() override;
    bool is_running() const override;
    std::string init_error() const override;
    void set_viewport(const HostViewport& viewport) override;
    void pump() override;
    void draw(IFrameContext& frame) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;

    void on_key(const KeyEvent& event) override;
    void on_text_input(const TextInputEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

private:
    struct PendingToast
    {
        gui::ToastLevel level;
        std::string message;
        float duration_s;
    };

    void refresh();
    void flush_atlas_if_dirty();

    IHostCallbacks* callbacks_ = nullptr;
    IGridRenderer* renderer_ = nullptr;
    TextService* text_service_ = nullptr;
    std::unique_ptr<IGridHandle> handle_;
    int pixel_w_ = 0;
    int pixel_h_ = 0;

    std::vector<gui::ToastEntry> active_;
    std::chrono::steady_clock::time_point last_tick_{};

    std::mutex pending_mutex_;
    std::vector<PendingToast> pending_;
};

} // namespace draxul
