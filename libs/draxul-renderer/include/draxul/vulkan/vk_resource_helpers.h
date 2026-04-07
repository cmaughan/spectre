#pragma once

#include <draxul/pending_atlas_upload.h>

#include <cstddef>
#include <utility>

namespace draxul
{

enum class BufferResizeResult
{
    Unchanged,
    Resized,
    Failed,
};

template <typename BufferHandle, typename AllocationHandle>
struct OwnedMappedBuffer
{
    BufferHandle buffer{};
    AllocationHandle allocation{};
    void* mapped = nullptr;
    size_t size = 0;
};

template <typename BufferState, typename CreateFn, typename DestroyFn>
BufferResizeResult ensure_buffer_size(BufferState& current, size_t required_size, CreateFn&& create_buffer, DestroyFn&& destroy_buffer)
{
    if (required_size <= current.size)
        return BufferResizeResult::Unchanged;

    BufferState replacement{};
    if (!create_buffer(required_size, replacement))
        return BufferResizeResult::Failed;

    if (current.buffer != BufferState{}.buffer)
        destroy_buffer(current);

    current = std::move(replacement);
    return BufferResizeResult::Resized;
}

} // namespace draxul
