#pragma once

#include <string>
#include <vulkan/vulkan.h>

namespace draxul
{

class VkContext;

class VkPipelineManager
{
public:
    bool initialize(VkDevice device, VkRenderPass render_pass, const std::string& shader_dir);
    void shutdown(VkDevice device);
    void swap(VkPipelineManager& other) noexcept;

    VkPipeline bg_pipeline() const
    {
        return bg_pipeline_;
    }
    VkPipeline fg_pipeline() const
    {
        return fg_pipeline_;
    }
    VkPipelineLayout bg_layout() const
    {
        return bg_layout_;
    }
    VkPipelineLayout fg_layout() const
    {
        return fg_layout_;
    }
    VkDescriptorSetLayout bg_desc_layout() const
    {
        return bg_desc_layout_;
    }
    VkDescriptorSetLayout fg_desc_layout() const
    {
        return fg_desc_layout_;
    }

private:
    static VkShaderModule load_shader(VkDevice device, const std::string& path);
    void reset_objects(VkDevice device);

    VkPipeline bg_pipeline_ = VK_NULL_HANDLE;
    VkPipeline fg_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout bg_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout fg_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout bg_desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout fg_desc_layout_ = VK_NULL_HANDLE;
};

} // namespace draxul
