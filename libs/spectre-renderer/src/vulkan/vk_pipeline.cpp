#include "vk_pipeline.h"
#include "vk_context.h"
#include <cstdio>
#include <fstream>
#include <vector>

namespace spectre {

VkShaderModule VkPipelineManager::load_shader(VkDevice device, const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "Failed to open shader: %s\n", path.c_str());
        return VK_NULL_HANDLE;
    }

    size_t size = file.tellg();
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), size);

    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create shader module: %s\n", path.c_str());
        return VK_NULL_HANDLE;
    }
    return module;
}

bool VkPipelineManager::initialize(VkContext& ctx, const std::string& shader_dir) {
    VkDevice device = ctx.device();

    // Load shaders
    auto bg_vert = load_shader(device, shader_dir + "/grid_bg.vert.spv");
    auto bg_frag = load_shader(device, shader_dir + "/grid_bg.frag.spv");
    auto fg_vert = load_shader(device, shader_dir + "/grid_fg.vert.spv");
    auto fg_frag = load_shader(device, shader_dir + "/grid_fg.frag.spv");

    if (!bg_vert || !bg_frag || !fg_vert || !fg_frag) return false;

    // Push constant range: screen_size (vec2), cell_size (vec2)
    VkPushConstantRange push_range = {};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(float) * 4;

    // BG descriptor set layout: just SSBO
    {
        VkDescriptorSetLayoutBinding ssbo_binding = {};
        ssbo_binding.binding = 0;
        ssbo_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ssbo_binding.descriptorCount = 1;
        ssbo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 1;
        layout_ci.pBindings = &ssbo_binding;
        vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &bg_desc_layout_);
    }

    // FG descriptor set layout: SSBO + atlas sampler
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 2;
        layout_ci.pBindings = bindings;
        vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &fg_desc_layout_);
    }

    // Pipeline layouts
    {
        VkPipelineLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &bg_desc_layout_;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;
        vkCreatePipelineLayout(device, &layout_ci, nullptr, &bg_layout_);

        layout_ci.pSetLayouts = &fg_desc_layout_;
        vkCreatePipelineLayout(device, &layout_ci, nullptr, &fg_layout_);
    }

    // Common pipeline state
    VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    // BG pipeline: no blending (opaque)
    {
        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = bg_vert;
        stages[0].pName = "main";
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = bg_frag;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vertex_input;
        pi.pInputAssemblyState = &input_assembly;
        pi.pViewportState = &viewport_state;
        pi.pRasterizationState = &raster;
        pi.pMultisampleState = &multisample;
        pi.pColorBlendState = &blend;
        pi.pDynamicState = &dynamic_state;
        pi.layout = bg_layout_;
        pi.renderPass = ctx.render_pass();
        pi.subpass = 0;

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &bg_pipeline_);
    }

    // FG pipeline: alpha blending
    {
        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = fg_vert;
        stages[0].pName = "main";
        stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fg_frag;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vertex_input;
        pi.pInputAssemblyState = &input_assembly;
        pi.pViewportState = &viewport_state;
        pi.pRasterizationState = &raster;
        pi.pMultisampleState = &multisample;
        pi.pColorBlendState = &blend;
        pi.pDynamicState = &dynamic_state;
        pi.layout = fg_layout_;
        pi.renderPass = ctx.render_pass();
        pi.subpass = 0;

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &fg_pipeline_);
    }

    // Cleanup shader modules
    vkDestroyShaderModule(device, bg_vert, nullptr);
    vkDestroyShaderModule(device, bg_frag, nullptr);
    vkDestroyShaderModule(device, fg_vert, nullptr);
    vkDestroyShaderModule(device, fg_frag, nullptr);

    return true;
}

void VkPipelineManager::shutdown(VkDevice device) {
    if (bg_pipeline_) vkDestroyPipeline(device, bg_pipeline_, nullptr);
    if (fg_pipeline_) vkDestroyPipeline(device, fg_pipeline_, nullptr);
    if (bg_layout_) vkDestroyPipelineLayout(device, bg_layout_, nullptr);
    if (fg_layout_) vkDestroyPipelineLayout(device, fg_layout_, nullptr);
    if (bg_desc_layout_) vkDestroyDescriptorSetLayout(device, bg_desc_layout_, nullptr);
    if (fg_desc_layout_) vkDestroyDescriptorSetLayout(device, fg_desc_layout_, nullptr);
}

} // namespace spectre
