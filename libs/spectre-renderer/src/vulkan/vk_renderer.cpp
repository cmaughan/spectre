#include "vk_renderer.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <spectre/window.h>

namespace spectre
{

bool VkRenderer::initialize(IWindow& window)
{
    if (!ctx_.initialize(window.native_handle()))
        return false;

    auto [w, h] = window.size_pixels();
    pixel_w_ = w;
    pixel_h_ = h;

    if (!atlas_.initialize(ctx_))
        return false;

    size_t initial_buf = 80 * 24 * sizeof(GpuCell);
    if (!grid_buffer_.initialize(ctx_, initial_buf))
        return false;

    if (!pipeline_.initialize(ctx_, "shaders"))
        return false;

    if (!create_command_buffers())
        return false;
    if (!create_sync_objects())
        return false;
    create_descriptor_pool();
    update_all_descriptor_sets();

    return true;
}

void VkRenderer::shutdown()
{
    if (ctx_.device())
        vkDeviceWaitIdle(ctx_.device());

    for (auto& sem : image_available_sem_)
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    for (auto& sem : render_finished_sem_)
        vkDestroySemaphore(ctx_.device(), sem, nullptr);
    for (auto& fence : in_flight_fences_)
        vkDestroyFence(ctx_.device(), fence, nullptr);
    if (cmd_pool_)
        vkDestroyCommandPool(ctx_.device(), cmd_pool_, nullptr);
    if (desc_pool_)
        vkDestroyDescriptorPool(ctx_.device(), desc_pool_, nullptr);

    grid_buffer_.shutdown(ctx_.allocator());
    pipeline_.shutdown(ctx_.device());
    atlas_.shutdown(ctx_);
    ctx_.shutdown();
}

bool VkRenderer::create_sync_objects()
{
    image_available_sem_.resize(MAX_FRAMES_IN_FLIGHT);
    render_finished_sem_.resize(MAX_FRAMES_IN_FLIGHT);
    in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sem_ci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkCreateSemaphore(ctx_.device(), &sem_ci, nullptr, &image_available_sem_[i]);
        vkCreateSemaphore(ctx_.device(), &sem_ci, nullptr, &render_finished_sem_[i]);
        vkCreateFence(ctx_.device(), &fence_ci, nullptr, &in_flight_fences_[i]);
    }
    return true;
}

bool VkRenderer::create_command_buffers()
{
    VkCommandPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool_ci.queueFamilyIndex = ctx_.graphics_queue_family();
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(ctx_.device(), &pool_ci, nullptr, &cmd_pool_);

    cmd_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc_info.commandPool = cmd_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
    vkAllocateCommandBuffers(ctx_.device(), &alloc_info, cmd_buffers_.data());

    return true;
}

void VkRenderer::create_descriptor_pool()
{
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)(MAX_FRAMES_IN_FLIGHT * 2) },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)MAX_FRAMES_IN_FLIGHT },
    };

    VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pool_ci.maxSets = MAX_FRAMES_IN_FLIGHT * 2;
    pool_ci.poolSizeCount = 2;
    pool_ci.pPoolSizes = pool_sizes;
    pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    vkCreateDescriptorPool(ctx_.device(), &pool_ci, nullptr, &desc_pool_);

    bg_desc_sets_.resize(MAX_FRAMES_IN_FLIGHT);
    fg_desc_sets_.resize(MAX_FRAMES_IN_FLIGHT);

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        VkDescriptorSetLayout bg_layout = pipeline_.bg_desc_layout();
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = desc_pool_;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &bg_layout;
        vkAllocateDescriptorSets(ctx_.device(), &alloc_info, &bg_desc_sets_[i]);

        VkDescriptorSetLayout fg_layout = pipeline_.fg_desc_layout();
        alloc_info.pSetLayouts = &fg_layout;
        vkAllocateDescriptorSets(ctx_.device(), &alloc_info, &fg_desc_sets_[i]);
    }
}

void VkRenderer::update_descriptor_sets_for_frame(int frame)
{
    VkDescriptorBufferInfo buf_info = {};
    buf_info.buffer = grid_buffer_.buffer();
    buf_info.offset = 0;
    buf_info.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet writes[3] = {};
    writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[0].dstSet = bg_desc_sets_[frame];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &buf_info;

    writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[1].dstSet = fg_desc_sets_[frame];
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &buf_info;

    VkDescriptorImageInfo img_info = {};
    img_info.sampler = atlas_.sampler();
    img_info.imageView = atlas_.image_view();
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    writes[2].dstSet = fg_desc_sets_[frame];
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &img_info;

    vkUpdateDescriptorSets(ctx_.device(), 3, writes, 0, nullptr);
}

void VkRenderer::update_all_descriptor_sets()
{
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        update_descriptor_sets_for_frame(i);
    }
    needs_descriptor_update_ = false;
}

void VkRenderer::set_grid_size(int cols, int rows)
{
    grid_cols_ = cols;
    grid_rows_ = rows;
    cursor_applied_ = false;
    cursor_overlay_active_ = false;

    size_t required = ((size_t)cols * rows + 1) * sizeof(GpuCell);
    if (grid_buffer_.ensure_size(ctx_.allocator(), ctx_.device(), required))
    {
        needs_descriptor_update_ = true;
    }

    gpu_cells_.resize(cols * rows);
    memset(gpu_cells_.data(), 0, gpu_cells_.size() * sizeof(GpuCell));

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            auto& cell = gpu_cells_[r * cols + c];
            cell.pos_x = (float)(c * cell_w_ + padding_);
            cell.pos_y = (float)(r * cell_h_ + padding_);
            cell.size_x = (float)cell_w_;
            cell.size_y = (float)cell_h_;
            cell.bg_r = 0.1f;
            cell.bg_g = 0.1f;
            cell.bg_b = 0.1f;
            cell.bg_a = 1.0f;
            cell.fg_r = 1.0f;
            cell.fg_g = 1.0f;
            cell.fg_b = 1.0f;
            cell.fg_a = 1.0f;
        }
    }

    memcpy(grid_buffer_.mapped(), gpu_cells_.data(), gpu_cells_.size() * sizeof(GpuCell));
}

void VkRenderer::update_cells(std::span<const CellUpdate> updates)
{
    restore_cursor();

    for (const auto& u : updates)
    {
        if (u.col < 0 || u.col >= grid_cols_ || u.row < 0 || u.row >= grid_rows_)
            continue;
        auto& cell = gpu_cells_[u.row * grid_cols_ + u.col];
        cell.bg_r = u.bg.r;
        cell.bg_g = u.bg.g;
        cell.bg_b = u.bg.b;
        cell.bg_a = u.bg.a;
        cell.fg_r = u.fg.r;
        cell.fg_g = u.fg.g;
        cell.fg_b = u.fg.b;
        cell.fg_a = u.fg.a;
        cell.uv_x0 = u.glyph.u0;
        cell.uv_y0 = u.glyph.v0;
        cell.uv_x1 = u.glyph.u1;
        cell.uv_y1 = u.glyph.v1;
        cell.glyph_offset_x = (float)u.glyph.bearing_x;
        cell.glyph_offset_y = (float)(cell_h_ - ascender_ + u.glyph.bearing_y);
        cell.glyph_size_x = (float)u.glyph.width;
        cell.glyph_size_y = (float)u.glyph.height;
        cell.style_flags = u.style_flags;
    }

    memcpy(grid_buffer_.mapped(), gpu_cells_.data(), gpu_cells_.size() * sizeof(GpuCell));
}

void VkRenderer::set_atlas_texture(const uint8_t* data, int w, int h)
{
    atlas_.upload(ctx_, data, w, h);
    needs_descriptor_update_ = true;
}

void VkRenderer::update_atlas_region(int x, int y, int w, int h, const uint8_t* data)
{
    atlas_.upload_region(ctx_, x, y, w, h, data);
}

void VkRenderer::set_cursor(int col, int row, const CursorStyle& style)
{
    restore_cursor();

    cursor_col_ = col;
    cursor_row_ = row;
    cursor_style_ = style;
}

void VkRenderer::resize(int pixel_w, int pixel_h)
{
    pixel_w_ = pixel_w;
    pixel_h_ = pixel_h;
    framebuffer_resized_ = true;
}

std::pair<int, int> VkRenderer::cell_size_pixels() const
{
    return { cell_w_, cell_h_ };
}

void VkRenderer::set_cell_size(int w, int h)
{
    cell_w_ = w;
    cell_h_ = h;
}

void VkRenderer::set_ascender(int a)
{
    ascender_ = a;
}

bool VkRenderer::begin_frame()
{
    vkWaitForFences(ctx_.device(), 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);

    restore_cursor();

    VkResult result = vkAcquireNextImageKHR(
        ctx_.device(), ctx_.swapchain().swapchain, UINT64_MAX,
        image_available_sem_[current_frame_], VK_NULL_HANDLE, &current_image_);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        ctx_.recreate_swapchain(pixel_w_, pixel_h_);
        pipeline_.shutdown(ctx_.device());
        pipeline_.initialize(ctx_, "shaders");
        if (desc_pool_)
        {
            vkDestroyDescriptorPool(ctx_.device(), desc_pool_, nullptr);
            desc_pool_ = VK_NULL_HANDLE;
        }
        create_descriptor_pool();
        update_all_descriptor_sets();
        return false;
    }

    if (needs_descriptor_update_)
    {
        update_descriptor_sets_for_frame(current_frame_);
        desc_update_pending_frames_ |= ((1 << MAX_FRAMES_IN_FLIGHT) - 1);
        desc_update_pending_frames_ &= ~(1 << current_frame_);
        if (desc_update_pending_frames_ == 0)
            needs_descriptor_update_ = false;
    }
    else if (desc_update_pending_frames_ & (1 << current_frame_))
    {
        update_descriptor_sets_for_frame(current_frame_);
        desc_update_pending_frames_ &= ~(1 << current_frame_);
    }

    vkResetFences(ctx_.device(), 1, &in_flight_fences_[current_frame_]);
    vkResetCommandBuffer(cmd_buffers_[current_frame_], 0);

    return true;
}

void VkRenderer::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index)
{
    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(cmd, &begin_info);

    VkClearValue clear_value = {};
    clear_value.color = { { 0.1f, 0.1f, 0.1f, 1.0f } };

    VkRenderPassBeginInfo rp_begin = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rp_begin.renderPass = ctx_.render_pass();
    rp_begin.framebuffer = ctx_.swapchain().framebuffers[image_index];
    rp_begin.renderArea.extent = ctx_.swapchain().extent;
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_value;

    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = (float)ctx_.swapchain().extent.width;
    viewport.height = (float)ctx_.swapchain().extent.height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = ctx_.swapchain().extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    int total_cells = grid_cols_ * grid_rows_;
    int bg_instances = total_cells + (cursor_overlay_active_ ? 1 : 0);

    if (total_cells > 0)
    {
        float push_data[4] = {
            (float)ctx_.swapchain().extent.width,
            (float)ctx_.swapchain().extent.height,
            (float)cell_w_,
            (float)cell_h_
        };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.bg_pipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.bg_layout(),
            0, 1, &bg_desc_sets_[current_frame_], 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_.bg_layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
        vkCmdDraw(cmd, 6, bg_instances, 0, 0);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.fg_pipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.fg_layout(),
            0, 1, &fg_desc_sets_[current_frame_], 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_.fg_layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_data), push_data);
        vkCmdDraw(cmd, 6, total_cells, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void VkRenderer::apply_cursor()
{
    if (cursor_col_ < 0 || cursor_col_ >= grid_cols_ || cursor_row_ < 0 || cursor_row_ >= grid_rows_)
        return;

    int idx = cursor_row_ * grid_cols_ + cursor_col_;
    auto& cell = gpu_cells_[idx];

    cursor_saved_cell_ = cell;
    cursor_applied_ = true;
    cursor_overlay_active_ = false;

    if (cursor_style_.shape == CursorShape::Block)
    {
        if (cursor_style_.use_explicit_colors)
        {
            cell.fg_r = cursor_style_.fg.r;
            cell.fg_g = cursor_style_.fg.g;
            cell.fg_b = cursor_style_.fg.b;
            cell.fg_a = cursor_style_.fg.a;
            cell.bg_r = cursor_style_.bg.r;
            cell.bg_g = cursor_style_.bg.g;
            cell.bg_b = cursor_style_.bg.b;
            cell.bg_a = cursor_style_.bg.a;
        }
        else
        {
            std::swap(cell.fg_r, cell.bg_r);
            std::swap(cell.fg_g, cell.bg_g);
            std::swap(cell.fg_b, cell.bg_b);
            std::swap(cell.fg_a, cell.bg_a);
        }

        memcpy((char*)grid_buffer_.mapped() + idx * sizeof(GpuCell),
            &cell, sizeof(GpuCell));
    }
    else
    {
        int overlay_idx = grid_cols_ * grid_rows_;
        GpuCell overlay = {};
        overlay.bg_r = cursor_style_.bg.r;
        overlay.bg_g = cursor_style_.bg.g;
        overlay.bg_b = cursor_style_.bg.b;
        overlay.bg_a = cursor_style_.bg.a;

        int percentage = cursor_style_.cell_percentage;
        if (percentage <= 0)
            percentage = (cursor_style_.shape == CursorShape::Vertical) ? 25 : 20;

        if (cursor_style_.shape == CursorShape::Vertical)
        {
            overlay.pos_x = cell.pos_x;
            overlay.pos_y = cell.pos_y;
            overlay.size_x = std::max(1.0f, cell.size_x * percentage / 100.0f);
            overlay.size_y = cell.size_y;
        }
        else
        {
            overlay.pos_x = cell.pos_x;
            overlay.size_y = std::max(1.0f, cell.size_y * percentage / 100.0f);
            overlay.pos_y = cell.pos_y + cell.size_y - overlay.size_y;
            overlay.size_x = cell.size_x;
        }

        memcpy((char*)grid_buffer_.mapped() + overlay_idx * sizeof(GpuCell),
            &overlay, sizeof(GpuCell));
        cursor_overlay_active_ = true;
    }
}

void VkRenderer::restore_cursor()
{
    if (!cursor_applied_)
        return;
    cursor_applied_ = false;
    cursor_overlay_active_ = false;

    if (cursor_col_ < 0 || cursor_col_ >= grid_cols_ || cursor_row_ < 0 || cursor_row_ >= grid_rows_)
        return;

    int idx = cursor_row_ * grid_cols_ + cursor_col_;
    gpu_cells_[idx] = cursor_saved_cell_;
    memcpy((char*)grid_buffer_.mapped() + idx * sizeof(GpuCell),
        &cursor_saved_cell_, sizeof(GpuCell));
}

void VkRenderer::end_frame()
{
    apply_cursor();
    record_command_buffer(cmd_buffers_[current_frame_], current_image_);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &image_available_sem_[current_frame_];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buffers_[current_frame_];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished_sem_[current_frame_];

    vkQueueSubmit(ctx_.graphics_queue(), 1, &submit, in_flight_fences_[current_frame_]);

    VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished_sem_[current_frame_];
    present.swapchainCount = 1;
    present.pSwapchains = &ctx_.swapchain().swapchain;
    present.pImageIndices = &current_image_;

    VkResult result = vkQueuePresentKHR(ctx_.graphics_queue(), &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized_)
    {
        framebuffer_resized_ = false;
        ctx_.recreate_swapchain(pixel_w_, pixel_h_);
        pipeline_.shutdown(ctx_.device());
        pipeline_.initialize(ctx_, "shaders");
        if (desc_pool_)
        {
            vkDestroyDescriptorPool(ctx_.device(), desc_pool_, nullptr);
            desc_pool_ = VK_NULL_HANDLE;
        }
        create_descriptor_pool();
        update_all_descriptor_sets();
    }

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace spectre
