#pragma once

#include <draxul/base_renderer.h>
#include <functional>
#include <memory>

struct NVGcontext;

namespace draxul
{

// Callback that hosts provide to draw NanoVG primitives each frame.
// Called with a valid NVGcontext* between nvgBeginFrame/nvgEndFrame.
using NanoVGDrawFn = std::function<void(NVGcontext* vg, int width, int height)>;

// INanoVGPass — an IRenderPass that provides NanoVG 2D vector drawing.
// Hosts own one of these and set a draw callback each frame.
// The pass handles all platform-specific NanoVG context management.
//
// Usage:
//   nanovg_pass_->set_draw_callback([](NVGcontext* vg, int w, int h) {
//       nvgBeginPath(vg);
//       nvgRoundedRect(vg, 10, 10, 200, 40, 6);
//       nvgFillColor(vg, nvgRGBA(50, 50, 60, 230));
//       nvgFill(vg);
//   });
//   frame.record_render_pass(*nanovg_pass_, viewport);
//
class INanoVGPass : public IRenderPass
{
public:
    ~INanoVGPass() override = default;

    // Set the draw function for the next frame. The callback receives a live
    // NVGcontext* and the viewport dimensions in pixels.
    virtual void set_draw_callback(NanoVGDrawFn fn) = 0;
};

// Factory — returns the platform-appropriate INanoVGPass.
// On Metal: creates a pass with a custom Metal NanoVG backend.
// On Vulkan: creates a pass with a custom Vulkan NanoVG backend.
// The backend is lazily initialized on the first record_prepass() call.
std::unique_ptr<INanoVGPass> create_nanovg_pass();

} // namespace draxul
