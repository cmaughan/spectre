#pragma once

#include <draxul/host.h>
#include <draxul/ui_panel.h>
#include <memory>
#include <optional>

namespace draxul
{

class DiagnosticsPanelHost final : public IHost
{
public:
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
    void on_mouse_button(const MouseButtonEvent& event) override;
    void on_mouse_move(const MouseMoveEvent& event) override;
    void on_mouse_wheel(const MouseWheelEvent& event) override;

    bool dispatch_action(std::string_view action) override;
    void request_close() override;
    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;
    void attach_imgui_host(IImGuiHost& host) override;
    void set_imgui_font(const std::string& path, float size_pixels) override;

    UiPanel& panel()
    {
        return panel_;
    }
    const UiPanel& panel() const
    {
        return panel_;
    }

    void set_visible(bool visible);
    bool visible() const;
    void set_window_metrics(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, float pixel_scale = 1.0f);
    const PanelLayout& layout() const;
    void update_diagnostic_state(const DiagnosticPanelState& state);
    std::optional<std::chrono::steady_clock::time_point> last_render_time() const
    {
        return last_render_time_;
    }

private:
    UiPanel panel_;
    IHostCallbacks* callbacks_ = nullptr;
    HostViewport viewport_;
    std::optional<std::chrono::steady_clock::time_point> last_draw_time_;
    std::optional<std::chrono::steady_clock::time_point> last_render_time_;
};

} // namespace draxul
