#include "isometric_scene_pass.h"

#include "mesh_library.h"
#include "vk_render_context.h"
#include <algorithm>
#include <cstring>
#include <draxul/log.h>
#include <draxul/runtime_path.h>
#include <fstream>
#include <vector>

namespace draxul
{

namespace
{

struct alignas(16) FrameUniforms
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec4 light_dir{ -0.5f, -1.0f, -0.3f, 0.0f };
};

struct alignas(16) ObjectPushConstants
{
    glm::mat4 world{ 1.0f };
    glm::vec4 color{ 1.0f };
};

struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t size = 0;
};

struct MeshBuffers
{
    Buffer vertices;
    Buffer indices;
    uint32_t index_count = 0;
};

struct BufferSlice
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t offset = 0;
};

struct MeshSlice
{
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceSize index_offset = 0;
    uint32_t index_count = 0;
};

struct TransientBufferArena
{
    Buffer buffer;
    size_t head = 0;

    void reset()
    {
        head = 0;
    }
};

struct TransientGeometryArena
{
    TransientBufferArena vertices;
    TransientBufferArena indices;

    void reset()
    {
        vertices.reset();
        indices.reset();
    }
};

struct FrameResources
{
    Buffer frame_uniforms;
    TransientGeometryArena geometry_arena;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

bool same_grid_spec(const FloorGridSpec& a, const FloorGridSpec& b)
{
    return a.enabled == b.enabled
        && a.min_x == b.min_x
        && a.max_x == b.max_x
        && a.min_z == b.min_z
        && a.max_z == b.max_z
        && a.tile_size == b.tile_size
        && a.line_width == b.line_width
        && a.y == b.y
        && a.color.x == b.color.x
        && a.color.y == b.color.y
        && a.color.z == b.color.z
        && a.color.w == b.color.w;
}

void destroy_buffer(VmaAllocator allocator, Buffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

bool create_mapped_buffer(VmaAllocator allocator, size_t size, VkBufferUsageFlags usage, Buffer& buffer)
{
    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = size;
    buf_ci.usage = usage;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info = {};
    if (vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &buffer.buffer, &buffer.allocation, &alloc_info) != VK_SUCCESS)
        return false;

    buffer.mapped = alloc_info.pMappedData;
    buffer.size = size;
    return true;
}

bool upload_mesh(VmaAllocator allocator, const MeshData& mesh, MeshBuffers& gpu_mesh)
{
    const size_t vertex_bytes = mesh.vertices.size() * sizeof(SceneVertex);
    const size_t index_bytes = mesh.indices.size() * sizeof(uint16_t);

    if (!create_mapped_buffer(allocator, vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu_mesh.vertices))
        return false;
    if (!create_mapped_buffer(allocator, index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gpu_mesh.indices))
        return false;

    std::memcpy(gpu_mesh.vertices.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(gpu_mesh.indices.mapped, mesh.indices.data(), index_bytes);
    vmaFlushAllocation(allocator, gpu_mesh.vertices.allocation, 0, vertex_bytes);
    vmaFlushAllocation(allocator, gpu_mesh.indices.allocation, 0, index_bytes);
    gpu_mesh.index_count = static_cast<uint32_t>(mesh.indices.size());
    return true;
}

size_t align_up(size_t value, size_t alignment)
{
    if (alignment <= 1)
        return value;
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

size_t grow_capacity(size_t current_size, size_t required_size, size_t minimum_size)
{
    if (current_size == 0)
        return std::max(required_size, minimum_size);
    return std::max(required_size, std::max(current_size * 2, minimum_size));
}

bool ensure_mapped_buffer_capacity(VmaAllocator allocator, size_t required_size,
    VkBufferUsageFlags usage, Buffer& buffer, size_t minimum_size)
{
    if (required_size == 0 || required_size <= buffer.size)
        return true;

    Buffer replacement;
    if (!create_mapped_buffer(allocator, grow_capacity(buffer.size, required_size, minimum_size), usage, replacement))
        return false;

    destroy_buffer(allocator, buffer);
    buffer = std::move(replacement);
    return true;
}

bool reserve_transient_buffer(VmaAllocator allocator, TransientBufferArena& arena, size_t size,
    size_t alignment, VkBufferUsageFlags usage, size_t minimum_size, BufferSlice& slice)
{
    if (size == 0)
    {
        slice = {};
        return true;
    }

    const size_t offset = align_up(arena.head, alignment);
    const size_t required_size = offset + size;
    if (!ensure_mapped_buffer_capacity(allocator, required_size, usage, arena.buffer, minimum_size))
        return false;

    slice.buffer = arena.buffer.buffer;
    slice.allocation = arena.buffer.allocation;
    slice.offset = offset;
    slice.mapped = static_cast<char*>(arena.buffer.mapped) + offset;
    arena.head = required_size;
    return true;
}

bool stream_transient_mesh(VmaAllocator allocator, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice)
{
    constexpr size_t kMinimumVertexArenaBytes = 16 * 1024;
    constexpr size_t kMinimumIndexArenaBytes = 4 * 1024;

    const size_t vertex_bytes = mesh.vertices.size() * sizeof(SceneVertex);
    const size_t index_bytes = mesh.indices.size() * sizeof(uint16_t);
    if (vertex_bytes == 0 || index_bytes == 0)
    {
        slice = {};
        return true;
    }

    BufferSlice vertex_slice;
    if (!reserve_transient_buffer(allocator, arena.vertices, vertex_bytes, alignof(SceneVertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kMinimumVertexArenaBytes, vertex_slice))
    {
        return false;
    }

    BufferSlice index_slice;
    if (!reserve_transient_buffer(allocator, arena.indices, index_bytes, alignof(uint16_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kMinimumIndexArenaBytes, index_slice))
    {
        return false;
    }

    std::memcpy(vertex_slice.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(index_slice.mapped, mesh.indices.data(), index_bytes);
    vmaFlushAllocation(allocator, vertex_slice.allocation, vertex_slice.offset, vertex_bytes);
    vmaFlushAllocation(allocator, index_slice.allocation, index_slice.offset, index_bytes);

    slice.vertex_buffer = vertex_slice.buffer;
    slice.vertex_offset = static_cast<VkDeviceSize>(vertex_slice.offset);
    slice.index_buffer = index_slice.buffer;
    slice.index_offset = static_cast<VkDeviceSize>(index_slice.offset);
    slice.index_count = static_cast<uint32_t>(mesh.indices.size());
    return true;
}

void destroy_mesh(VmaAllocator allocator, MeshBuffers& mesh)
{
    destroy_buffer(allocator, mesh.vertices);
    destroy_buffer(allocator, mesh.indices);
    mesh.index_count = 0;
}

VkShaderModule load_shader(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to open shader %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create shader module %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return module;
}

glm::mat4 make_vulkan_projection(glm::mat4 proj)
{
    proj[1][1] *= -1.0f;
    return proj;
}

} // namespace

struct IsometricScenePass::State
{
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    MeshBuffers cube_mesh;
    MeshData cached_grid_mesh;
    FloorGridSpec cached_grid_spec;
    bool has_cached_grid_mesh = false;
    std::vector<FrameResources> frame_resources;
    uint32_t buffered_frame_count = 1;

    bool create_device_resources(uint32_t frame_count)
    {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 1;
        layout_ci.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &descriptor_set_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create descriptor set layout");
            return false;
        }

        VkDescriptorPoolSize pool_size = {};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = frame_count;

        VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_ci.maxSets = frame_count;
        pool_ci.poolSizeCount = 1;
        pool_ci.pPoolSizes = &pool_size;
        if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &descriptor_pool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create descriptor pool");
            return false;
        }

        frame_resources.assign(frame_count, {});
        std::vector<VkDescriptorSetLayout> layouts(frame_count, descriptor_set_layout);
        std::vector<VkDescriptorSet> descriptor_sets(frame_count, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = frame_count;
        alloc_info.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets.data()) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to allocate descriptor set");
            return false;
        }

        for (uint32_t i = 0; i < frame_count; ++i)
        {
            auto& frame = frame_resources[i];
            frame.descriptor_set = descriptor_sets[i];

            if (!create_mapped_buffer(allocator, sizeof(FrameUniforms),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frame.frame_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create frame uniform buffer");
                return false;
            }

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = frame.frame_uniforms.buffer;
            buffer_info.range = sizeof(FrameUniforms);

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = frame.descriptor_set;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &buffer_info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        buffered_frame_count = frame_count;

        if (!upload_mesh(allocator, build_unit_cube_mesh(), cube_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload cube mesh");
            return false;
        }
        return true;
    }

    bool ensure_floor_grid(const FloorGridSpec& spec)
    {
        if (!spec.enabled)
        {
            cached_grid_mesh = {};
            cached_grid_spec = {};
            has_cached_grid_mesh = false;
            return true;
        }
        if (has_cached_grid_mesh && same_grid_spec(cached_grid_spec, spec))
            return true;

        cached_grid_mesh = build_outline_grid_mesh(spec);
        cached_grid_spec = spec;
        has_cached_grid_mesh = true;
        return true;
    }

    bool create_pipeline()
    {
        const auto shader_dir = bundled_asset_path("shaders");
        auto vert = load_shader(device, (shader_dir / "megacity_scene.vert.spv").string());
        auto frag = load_shader(device, (shader_dir / "megacity_scene.frag.spv").string());
        if (!vert || !frag)
        {
            if (vert)
                vkDestroyShaderModule(device, vert, nullptr);
            if (frag)
                vkDestroyShaderModule(device, frag, nullptr);
            return false;
        }

        VkPushConstantRange push_range = {};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(ObjectPushConstants);

        VkPipelineLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &descriptor_set_layout;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;
        if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create pipeline layout");
            vkDestroyShaderModule(device, vert, nullptr);
            vkDestroyShaderModule(device, frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(SceneVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributes[3] = {};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(SceneVertex, position);
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(SceneVertex, normal);
        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[2].offset = offsetof(SceneVertex, color);

        VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 3;
        vertex_input.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo pipeline_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_ci.stageCount = 2;
        pipeline_ci.pStages = stages;
        pipeline_ci.pVertexInputState = &vertex_input;
        pipeline_ci.pInputAssemblyState = &input_assembly;
        pipeline_ci.pViewportState = &viewport_state;
        pipeline_ci.pRasterizationState = &raster;
        pipeline_ci.pMultisampleState = &multisample;
        pipeline_ci.pDepthStencilState = &depth;
        pipeline_ci.pColorBlendState = &blend;
        pipeline_ci.pDynamicState = &dynamic;
        pipeline_ci.layout = pipeline_layout;
        pipeline_ci.renderPass = render_pass;
        pipeline_ci.subpass = 0;

        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline);
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        if (result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create graphics pipeline");
            return false;
        }

        return true;
    }

    bool ensure(const VkRenderContext& ctx)
    {
        const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
        if (pipeline != VK_NULL_HANDLE
            && render_pass == ctx.render_pass()
            && buffered_frame_count == frame_count)
            return true;

        destroy();
        device = ctx.device();
        allocator = ctx.allocator();
        render_pass = ctx.render_pass();
        buffered_frame_count = frame_count;

        return create_device_resources(frame_count) && create_pipeline();
    }

    void destroy()
    {
        if (allocator != VK_NULL_HANDLE)
        {
            destroy_mesh(allocator, cube_mesh);
            for (auto& frame : frame_resources)
            {
                destroy_buffer(allocator, frame.geometry_arena.vertices.buffer);
                destroy_buffer(allocator, frame.geometry_arena.indices.buffer);
                destroy_buffer(allocator, frame.frame_uniforms);
            }
        }
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);

        device = VK_NULL_HANDLE;
        allocator = VK_NULL_HANDLE;
        render_pass = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        frame_resources.clear();
        buffered_frame_count = 1;
        cached_grid_mesh = {};
        cached_grid_spec = {};
        has_cached_grid_mesh = false;
    }
};

IsometricScenePass::IsometricScenePass(int grid_width, int grid_height, float tile_size)
    : grid_width_(grid_width)
    , grid_height_(grid_height)
    , tile_size_(tile_size)
    , state_(std::make_unique<State>())
{
}

IsometricScenePass::~IsometricScenePass()
{
    state_->destroy();
}

void IsometricScenePass::record(IRenderContext& ctx)
{
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    auto cmd = static_cast<VkCommandBuffer>(ctx.native_command_buffer());
    if (!cmd)
        return;

    const uint32_t frame_index = vk_ctx->frame_index();
    if (!state_->ensure(*vk_ctx))
        return;
    if (frame_index >= state_->frame_resources.size())
        return;

    auto& frame_resources = state_->frame_resources[frame_index];
    frame_resources.geometry_arena.reset();

    MeshSlice grid_slice;
    if (!state_->ensure_floor_grid(scene_.floor_grid))
        return;
    if (state_->has_cached_grid_mesh
        && !stream_transient_mesh(vk_ctx->allocator(), state_->cached_grid_mesh,
            frame_resources.geometry_arena, grid_slice))
    {
        return;
    }

    FrameUniforms frame;
    frame.view = scene_.camera.view;
    frame.proj = make_vulkan_projection(scene_.camera.proj);
    frame.light_dir = scene_.camera.light_dir;
    std::memcpy(frame_resources.frame_uniforms.mapped, &frame, sizeof(frame));
    vmaFlushAllocation(vk_ctx->allocator(), frame_resources.frame_uniforms.allocation, 0, sizeof(frame));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->pipeline_layout,
        0, 1, &frame_resources.descriptor_set, 0, nullptr);

    for (const SceneObject& obj : scene_.objects)
    {
        const MeshBuffers* mesh = nullptr;
        switch (obj.mesh)
        {
        case MeshId::Cube:
            mesh = &state_->cube_mesh;
            break;
        case MeshId::Grid:
            continue;
        }
        if (!mesh || mesh->index_count == 0)
            continue;

        ObjectPushConstants push;
        push.world = obj.world;
        push.color = obj.color;

        VkBuffer vertex_buffer = mesh->vertices.buffer;
        VkDeviceSize vertex_offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
    }

    if (grid_slice.index_count > 0)
    {
        ObjectPushConstants push;
        push.world = glm::mat4(1.0f);
        push.color = scene_.floor_grid.color;

        VkBuffer vertex_buffer = grid_slice.vertex_buffer;
        VkDeviceSize vertex_offset = grid_slice.vertex_offset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, grid_slice.index_buffer, grid_slice.index_offset, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, grid_slice.index_count, 1, 0, 0, 0);
    }
}

} // namespace draxul
