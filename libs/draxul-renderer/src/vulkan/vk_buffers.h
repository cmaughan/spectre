#pragma once
#include <draxul/vulkan/vk_resource_helpers.h>

#include <cstdint>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul
{

class VkContext;

// Host-visible coherent SSBO for grid cell data
class VkGridBuffer
{
public:
    bool initialize(VkContext& ctx, size_t initial_size);
    void shutdown(VmaAllocator allocator);

    BufferResizeResult ensure_size(VmaAllocator allocator, size_t required_size);
    void flush_range(VmaAllocator allocator, VkDeviceSize offset, VkDeviceSize size) const;

    void* mapped() const
    {
        return buffer_.mapped;
    }
    VkBuffer buffer() const
    {
        return buffer_.buffer;
    }
    size_t size() const
    {
        return buffer_.size;
    }

private:
    using BufferState = OwnedMappedBuffer<VkBuffer, VmaAllocation>;

    BufferState buffer_;
};

// Staging buffer for atlas uploads
class VkStagingBuffer
{
public:
    bool initialize(VmaAllocator allocator, size_t size);
    void shutdown(VmaAllocator allocator);

    void* mapped() const
    {
        return mapped_;
    }
    VkBuffer buffer() const
    {
        return buffer_;
    }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
};

} // namespace draxul
