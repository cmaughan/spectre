#include "vk_atlas.h"
#include "vk_context.h"
#include <cstring>
#include <cstdio>

namespace spectre {

bool VkAtlas::initialize(VkContext& ctx) {
    VkDevice device = ctx.device();

    VkCommandPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool_ci.queueFamilyIndex = ctx.graphics_queue_family();
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device, &pool_ci, nullptr, &cmd_pool_);

    VkImageCreateInfo img_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = VK_FORMAT_R8_UNORM;
    img_ci.extent = { ATLAS_SIZE, ATLAS_SIZE, 1 };
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(ctx.allocator(), &img_ci, &alloc_ci, &image_, &allocation_, nullptr) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create atlas image\n");
        return false;
    }

    VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = image_;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = VK_FORMAT_R8_UNORM;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    view_ci.components.r = VK_COMPONENT_SWIZZLE_R;
    view_ci.components.g = VK_COMPONENT_SWIZZLE_R;
    view_ci.components.b = VK_COMPONENT_SWIZZLE_R;
    view_ci.components.a = VK_COMPONENT_SWIZZLE_R;
    vkCreateImageView(device, &view_ci, nullptr, &image_view_);

    VkSamplerCreateInfo samp_ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samp_ci.magFilter = VK_FILTER_LINEAR;
    samp_ci.minFilter = VK_FILTER_LINEAR;
    samp_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &samp_ci, nullptr, &sampler_);

    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = ATLAS_SIZE * ATLAS_SIZE;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo staging_ci = {};
    staging_ci.usage = VMA_MEMORY_USAGE_AUTO;
    staging_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo staging_info;
    vmaCreateBuffer(ctx.allocator(), &buf_ci, &staging_ci, &staging_buffer_, &staging_alloc_, &staging_info);
    staging_mapped_ = staging_info.pMappedData;

    transition_image_layout(ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return true;
}

void VkAtlas::shutdown(VkContext& ctx) {
    VkDevice device = ctx.device();
    if (staging_buffer_) vmaDestroyBuffer(ctx.allocator(), staging_buffer_, staging_alloc_);
    if (sampler_) vkDestroySampler(device, sampler_, nullptr);
    if (image_view_) vkDestroyImageView(device, image_view_, nullptr);
    if (image_) vmaDestroyImage(ctx.allocator(), image_, allocation_);
    if (cmd_pool_) vkDestroyCommandPool(device, cmd_pool_, nullptr);
}

void VkAtlas::upload(VkContext& ctx, const uint8_t* data, int w, int h) {
    memcpy(staging_mapped_, data, (size_t)w * h);

    transition_image_layout(ctx, current_layout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(ctx, staging_buffer_, 0, 0, w, h);
    transition_image_layout(ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VkAtlas::upload_region(VkContext& ctx, int x, int y, int w, int h, const uint8_t* data) {
    uint8_t* dst = static_cast<uint8_t*>(staging_mapped_);
    for (int row = 0; row < h; row++) {
        memcpy(dst + row * w, data + row * w, w);
    }

    transition_image_layout(ctx, current_layout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy_buffer_to_image(ctx, staging_buffer_, x, y, w, h);
    transition_image_layout(ctx, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

VkCommandBuffer VkAtlas::begin_single_command(VkContext& ctx) {
    VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc_info.commandPool = cmd_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx.device(), &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    return cmd;
}

void VkAtlas::end_single_command(VkContext& ctx, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    vkQueueSubmit(ctx.graphics_queue(), 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.graphics_queue());

    vkFreeCommandBuffers(ctx.device(), cmd_pool_, 1, &cmd);
}

void VkAtlas::transition_image_layout(VkContext& ctx, VkImageLayout old_layout, VkImageLayout new_layout) {
    auto cmd = begin_single_command(ctx);

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage, dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    end_single_command(ctx, cmd);

    current_layout_ = new_layout;
}

void VkAtlas::copy_buffer_to_image(VkContext& ctx, VkBuffer buffer, int x, int y, int w, int h) {
    auto cmd = begin_single_command(ctx);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { x, y, 0 };
    region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };

    vkCmdCopyBufferToImage(cmd, buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    end_single_command(ctx, cmd);
}

} // namespace spectre
