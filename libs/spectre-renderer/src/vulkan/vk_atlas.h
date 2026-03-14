#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace spectre {

class VkContext;
class VkStagingBuffer;

class VkAtlas {
public:
    static constexpr int ATLAS_SIZE = 2048;

    bool initialize(VkContext& ctx);
    void shutdown(VkContext& ctx);

    // Upload full atlas data
    void upload(VkContext& ctx, const uint8_t* data, int w, int h);

    // Upload a sub-region
    void upload_region(VkContext& ctx, int x, int y, int w, int h, const uint8_t* data);

    VkImageView image_view() const { return image_view_; }
    VkSampler sampler() const { return sampler_; }

private:
    void transition_image_layout(VkContext& ctx, VkImageLayout old_layout, VkImageLayout new_layout);
    void copy_buffer_to_image(VkContext& ctx, VkBuffer buffer, int x, int y, int w, int h);
    VkCommandBuffer begin_single_command(VkContext& ctx);
    void end_single_command(VkContext& ctx, VkCommandBuffer cmd);

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkBuffer staging_buffer_ = VK_NULL_HANDLE;
    VmaAllocation staging_alloc_ = VK_NULL_HANDLE;
    void* staging_mapped_ = nullptr;
    VkImageLayout current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace spectre
