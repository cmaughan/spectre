#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <cstdint>

namespace spectre {

class VkContext;

// Host-visible coherent SSBO for grid cell data
class VkGridBuffer {
public:
    bool initialize(VkContext& ctx, size_t initial_size);
    void shutdown(VmaAllocator allocator);

    // Resize buffer if needed. Returns true if resized.
    bool ensure_size(VmaAllocator allocator, VkDevice device, size_t required_size);

    void* mapped() const { return mapped_; }
    VkBuffer buffer() const { return buffer_; }
    size_t size() const { return size_; }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    size_t size_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

// Staging buffer for atlas uploads
class VkStagingBuffer {
public:
    bool initialize(VmaAllocator allocator, size_t size);
    void shutdown(VmaAllocator allocator);

    void* mapped() const { return mapped_; }
    VkBuffer buffer() const { return buffer_; }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
};

} // namespace spectre
