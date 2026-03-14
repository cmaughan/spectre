#include "vk_buffers.h"
#include "vk_context.h"
#include <cstdio>

namespace spectre {

bool VkGridBuffer::initialize(VkContext& ctx, size_t initial_size) {
    allocator_ = ctx.allocator();

    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = initial_size;
    buf_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info;
    if (vmaCreateBuffer(allocator_, &buf_ci, &alloc_ci, &buffer_, &allocation_, &alloc_info) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create grid SSBO\n");
        return false;
    }

    mapped_ = alloc_info.pMappedData;
    size_ = initial_size;
    return true;
}

void VkGridBuffer::shutdown(VmaAllocator allocator) {
    if (buffer_) {
        vmaDestroyBuffer(allocator, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mapped_ = nullptr;
    }
}

bool VkGridBuffer::ensure_size(VmaAllocator allocator, VkDevice device, size_t required_size) {
    if (required_size <= size_) return false;

    vmaDestroyBuffer(allocator, buffer_, allocation_);

    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = required_size;
    buf_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info;
    vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &buffer_, &allocation_, &alloc_info);
    mapped_ = alloc_info.pMappedData;
    size_ = required_size;
    return true;
}

bool VkStagingBuffer::initialize(VmaAllocator allocator, size_t size) {
    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = size;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info;
    if (vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &buffer_, &allocation_, &alloc_info) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create staging buffer\n");
        return false;
    }

    mapped_ = alloc_info.pMappedData;
    return true;
}

void VkStagingBuffer::shutdown(VmaAllocator allocator) {
    if (buffer_) {
        vmaDestroyBuffer(allocator, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mapped_ = nullptr;
    }
}

} // namespace spectre
