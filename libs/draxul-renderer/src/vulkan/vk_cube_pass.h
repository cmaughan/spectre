#pragma once
#include <string>
#include <vulkan/vulkan.h>

namespace draxul
{

// Pipeline state for the MegaCity spinning cube.
// Owned and lazily created by CubeRenderPass::State in megacity_render_vk.cpp.
// Created on first record() call and recreated when the VkRenderPass handle changes.
struct VkCubePass
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;

    bool create(VkDevice device, VkRenderPass render_pass, const std::string& shader_dir);
    void destroy(VkDevice device);
};

} // namespace draxul
