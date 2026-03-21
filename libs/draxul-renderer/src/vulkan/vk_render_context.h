#pragma once
// Internal Vulkan header — only included from Vulkan (.cpp) translation units.
#include <draxul/base_renderer.h>
#include <vulkan/vulkan.h>

namespace draxul
{

// Platform-specific IRenderContext for the Vulkan backend.
// Passed to IRenderPass::record() during end_frame() / record_command_buffer().
//
// native_render_encoder() returns nullptr — Vulkan has no encoder object.
// native_command_buffer() returns the VkCommandBuffer (cast to void*).
//
// Platform-specific code that knows it is running on Vulkan may static_cast
// to VkRenderContext to access device() and render_pass() for lazy pipeline
// creation and recreation detection.
class VkRenderContext : public IRenderContext
{
public:
    VkRenderContext(VkCommandBuffer cmd, VkDevice device, VkRenderPass render_pass, int w, int h)
        : cmd_(cmd)
        , device_(device)
        , render_pass_(render_pass)
        , w_(w)
        , h_(h)
    {
    }

    void* native_command_buffer() const override
    {
        return cmd_;
    }
    void* native_render_encoder() const override
    {
        return nullptr;
    }
    int width() const override
    {
        return w_;
    }
    int height() const override
    {
        return h_;
    }

    // Vulkan-specific extensions — cast from IRenderContext in Vk-specific code
    VkDevice device() const
    {
        return device_;
    }
    VkRenderPass render_pass() const
    {
        return render_pass_;
    }

private:
    VkCommandBuffer cmd_;
    VkDevice device_;
    VkRenderPass render_pass_;
    int w_;
    int h_;
};

} // namespace draxul
