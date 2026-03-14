#pragma once
#include <vulkan/vulkan.h>
#include <string>

namespace spectre {

class VkContext;

class VkPipelineManager {
public:
    bool initialize(VkContext& ctx, const std::string& shader_dir);
    void shutdown(VkDevice device);

    VkPipeline bg_pipeline() const { return bg_pipeline_; }
    VkPipeline fg_pipeline() const { return fg_pipeline_; }
    VkPipelineLayout bg_layout() const { return bg_layout_; }
    VkPipelineLayout fg_layout() const { return fg_layout_; }
    VkDescriptorSetLayout bg_desc_layout() const { return bg_desc_layout_; }
    VkDescriptorSetLayout fg_desc_layout() const { return fg_desc_layout_; }

private:
    VkShaderModule load_shader(VkDevice device, const std::string& path);

    VkPipeline bg_pipeline_ = VK_NULL_HANDLE;
    VkPipeline fg_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout bg_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout fg_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout bg_desc_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout fg_desc_layout_ = VK_NULL_HANDLE;
};

} // namespace spectre
