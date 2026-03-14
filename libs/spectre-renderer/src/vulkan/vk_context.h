#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

struct SDL_Window;

namespace spectre {

struct SwapchainInfo {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    std::vector<VkFramebuffer> framebuffers;
};

class VkContext {
public:
    bool initialize(SDL_Window* window);
    void shutdown();

    bool recreate_swapchain(int width, int height);
    void create_framebuffers();
    void destroy_swapchain();

    // Accessors
    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physical_device() const { return physical_device_; }
    VkDevice device() const { return device_; }
    VkQueue graphics_queue() const { return graphics_queue_; }
    uint32_t graphics_queue_family() const { return graphics_queue_family_; }
    VmaAllocator allocator() const { return allocator_; }
    VkRenderPass render_pass() const { return render_pass_; }
    const SwapchainInfo& swapchain() const { return swapchain_; }
    VkSurfaceKHR surface() const { return surface_; }

private:
    bool create_render_pass();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    SwapchainInfo swapchain_;
    SDL_Window* window_ = nullptr;
};

} // namespace spectre
