#include "isometric_scene_pass.h"

#include "mesh_library.h"
#include "objc_ref.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <cstring>
#include <draxul/log.h>
#import <simd/simd.h>
#include <vector>

namespace draxul
{

namespace
{

struct FrameUniforms
{
    simd_float4x4 view;
    simd_float4x4 proj;
    simd_float4 light_dir;
};

struct ObjectUniforms
{
    simd_float4x4 world;
    simd_float4 color;
};

struct MeshBuffers
{
    ObjCRef<id<MTLBuffer>> vertex_buffer;
    ObjCRef<id<MTLBuffer>> index_buffer;
    NSUInteger index_count = 0;
};

struct BufferSlice
{
    id<MTLBuffer> buffer = nil;
    NSUInteger offset = 0;
    void* mapped = nullptr;
};

struct MeshSlice
{
    id<MTLBuffer> vertex_buffer = nil;
    NSUInteger vertex_offset = 0;
    id<MTLBuffer> index_buffer = nil;
    NSUInteger index_offset = 0;
    NSUInteger index_count = 0;
};

struct TransientBufferArena
{
    ObjCRef<id<MTLBuffer>> buffer;
    NSUInteger head = 0;

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
    ObjCRef<id<MTLBuffer>> frame_uniforms;
    TransientGeometryArena geometry_arena;
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

simd_float4x4 to_simd_matrix(const glm::mat4& mat)
{
    simd_float4x4 out;
    for (int column = 0; column < 4; ++column)
    {
        out.columns[column] = simd_make_float4(
            mat[column][0],
            mat[column][1],
            mat[column][2],
            mat[column][3]);
    }
    return out;
}

bool upload_mesh(id<MTLDevice> device, const MeshData& mesh, MeshBuffers& buffers)
{
    id<MTLBuffer> vertex_buffer = [device newBufferWithBytes:mesh.vertices.data()
                                                      length:mesh.vertices.size() * sizeof(SceneVertex)
                                                     options:MTLResourceStorageModeShared];
    id<MTLBuffer> index_buffer = [device newBufferWithBytes:mesh.indices.data()
                                                     length:mesh.indices.size() * sizeof(uint16_t)
                                                    options:MTLResourceStorageModeShared];
    if (!vertex_buffer || !index_buffer)
        return false;

    buffers.vertex_buffer.reset(vertex_buffer);
    buffers.index_buffer.reset(index_buffer);
    buffers.index_count = static_cast<NSUInteger>(mesh.indices.size());
    return true;
}

NSUInteger align_up(NSUInteger value, NSUInteger alignment)
{
    if (alignment <= 1)
        return value;
    const NSUInteger remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

NSUInteger grow_capacity(NSUInteger current_size, NSUInteger required_size)
{
    if (current_size == 0)
        return required_size;
    return std::max(required_size, current_size * 2);
}

bool ensure_buffer_capacity(id<MTLDevice> device, NSUInteger required_size, ObjCRef<id<MTLBuffer>>& buffer)
{
    if (required_size == 0)
        return true;
    if (buffer && [buffer.get() length] >= required_size)
        return true;

    id<MTLBuffer> replacement = [device newBufferWithLength:grow_capacity(buffer ? [buffer.get() length] : 0, required_size)
                                                    options:MTLResourceStorageModeShared];
    if (!replacement)
        return false;

    buffer.reset(replacement);
    return true;
}

bool reserve_transient_buffer(id<MTLDevice> device, TransientBufferArena& arena,
    NSUInteger size, NSUInteger alignment, NSUInteger minimum_size, BufferSlice& slice)
{
    if (size == 0)
    {
        slice = {};
        return true;
    }

    const NSUInteger offset = align_up(arena.head, alignment);
    const NSUInteger required_size = offset + size;
    if (!ensure_buffer_capacity(device, std::max(required_size, minimum_size), arena.buffer))
        return false;

    slice.buffer = arena.buffer.get();
    slice.offset = offset;
    slice.mapped = static_cast<char*>([arena.buffer.get() contents]) + offset;
    arena.head = required_size;
    return true;
}

bool stream_transient_mesh(id<MTLDevice> device, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice)
{
    constexpr NSUInteger kMinimumVertexArenaBytes = 16 * 1024;
    constexpr NSUInteger kMinimumIndexArenaBytes = 4 * 1024;

    const NSUInteger vertex_bytes = static_cast<NSUInteger>(mesh.vertices.size() * sizeof(SceneVertex));
    const NSUInteger index_bytes = static_cast<NSUInteger>(mesh.indices.size() * sizeof(uint16_t));
    if (vertex_bytes == 0 || index_bytes == 0)
    {
        slice = {};
        return true;
    }

    BufferSlice vertex_slice;
    if (!reserve_transient_buffer(device, arena.vertices, vertex_bytes, alignof(SceneVertex),
            kMinimumVertexArenaBytes, vertex_slice))
    {
        return false;
    }

    BufferSlice index_slice;
    if (!reserve_transient_buffer(device, arena.indices, index_bytes, alignof(uint16_t),
            kMinimumIndexArenaBytes, index_slice))
    {
        return false;
    }

    std::memcpy(vertex_slice.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(index_slice.mapped, mesh.indices.data(), index_bytes);
    slice.vertex_buffer = vertex_slice.buffer;
    slice.vertex_offset = vertex_slice.offset;
    slice.index_buffer = index_slice.buffer;
    slice.index_offset = index_slice.offset;
    slice.index_count = static_cast<NSUInteger>(mesh.indices.size());
    return true;
}

} // namespace

struct IsometricScenePass::State
{
    ObjCRef<id<MTLRenderPipelineState>> pipeline;
    ObjCRef<id<MTLDepthStencilState>> depth_state;
    MeshBuffers cube_mesh;
    MeshData cached_grid_mesh;
    FloorGridSpec cached_grid_spec;
    bool has_cached_grid_mesh = false;
    std::vector<FrameResources> frame_resources;
    uint32_t buffered_frame_count = 1;
    bool initialized = false;

    bool init(id<MTLDevice> device, int grid_width, int grid_height, float tile_size)
    {
        if (initialized)
            return pipeline.get() != nil && depth_state.get() != nil;

        initialized = true;

        NSError* error = nil;
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* libPath = [exeDir stringByAppendingPathComponent:@"shaders/megacity_scene.metallib"];
        NSURL* libURL = [NSURL fileURLWithPath:libPath];

        id<MTLLibrary> library = [device newLibraryWithURL:libURL error:&error];
        if (!library)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to load megacity_scene.metallib from %s: %s",
                [libPath UTF8String],
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }

        id<MTLFunction> vert_fn = [library newFunctionWithName:@"scene_vertex"];
        id<MTLFunction> frag_fn = [library newFunctionWithName:@"scene_fragment"];
        if (!vert_fn || !frag_fn)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: scene_vertex or scene_fragment not found in shader library");
            return false;
        }

        MTLVertexDescriptor* vertex_desc = [[MTLVertexDescriptor alloc] init];
        vertex_desc.attributes[0].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[0].offset = offsetof(SceneVertex, position);
        vertex_desc.attributes[0].bufferIndex = 0;
        vertex_desc.attributes[1].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[1].offset = offsetof(SceneVertex, normal);
        vertex_desc.attributes[1].bufferIndex = 0;
        vertex_desc.attributes[2].format = MTLVertexFormatFloat3;
        vertex_desc.attributes[2].offset = offsetof(SceneVertex, color);
        vertex_desc.attributes[2].bufferIndex = 0;
        vertex_desc.layouts[0].stride = sizeof(SceneVertex);
        vertex_desc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vert_fn;
        desc.fragmentFunction = frag_fn;
        desc.vertexDescriptor = vertex_desc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        id<MTLRenderPipelineState> pipeline_state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "MegaCity: failed to create scene pipeline: %s",
                error ? [[error localizedDescription] UTF8String] : "unknown");
            return false;
        }
        pipeline.reset(pipeline_state);

        MTLDepthStencilDescriptor* depth_desc = [[MTLDepthStencilDescriptor alloc] init];
        depth_desc.depthCompareFunction = MTLCompareFunctionLessEqual;
        depth_desc.depthWriteEnabled = YES;
        depth_state.reset([device newDepthStencilStateWithDescriptor:depth_desc]);
        if (!depth_state)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to create depth state");
            return false;
        }

        if (!upload_mesh(device, build_unit_cube_mesh(), cube_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to upload cube mesh");
            return false;
        }
        DRAXUL_LOG_INFO(LogCategory::App, "MegaCity: scene pipeline initialized");
        return true;
    }

    bool ensure_frame_resources(id<MTLDevice> device, uint32_t frame_count)
    {
        frame_count = std::max(1u, frame_count);
        if (buffered_frame_count == frame_count && frame_resources.size() == frame_count)
            return true;

        frame_resources.clear();
        frame_resources.resize(frame_count);
        buffered_frame_count = frame_count;

        for (auto& frame : frame_resources)
        {
            if (!ensure_buffer_capacity(device, sizeof(FrameUniforms), frame.frame_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "MegaCity: failed to allocate frame uniform buffer");
                frame_resources.clear();
                buffered_frame_count = 1;
                return false;
            }
        }
        return true;
    }

    bool ensure_floor_grid(id<MTLDevice> device, const FloorGridSpec& spec)
    {
        (void)device;
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
};

IsometricScenePass::IsometricScenePass(int grid_width, int grid_height, float tile_size)
    : grid_width_(grid_width)
    , grid_height_(grid_height)
    , tile_size_(tile_size)
    , state_(std::make_unique<State>())
{
}

IsometricScenePass::~IsometricScenePass() = default;

void IsometricScenePass::record(IRenderContext& ctx)
{
    id<MTLCommandBuffer> cmd_buf = (__bridge id<MTLCommandBuffer>)ctx.native_command_buffer();
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)ctx.native_render_encoder();
    if (!cmd_buf || !encoder)
        return;

    const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
    if (!state_->init(cmd_buf.device, grid_width_, grid_height_, tile_size_))
        return;
    if (!state_->ensure_frame_resources(cmd_buf.device, frame_count))
        return;
    if (!state_->ensure_floor_grid(cmd_buf.device, scene_.floor_grid))
        return;

    const uint32_t frame_index = ctx.frame_index() % static_cast<uint32_t>(state_->frame_resources.size());
    auto& frame_resources = state_->frame_resources[frame_index];
    frame_resources.geometry_arena.reset();

    MeshSlice grid_slice;
    if (state_->has_cached_grid_mesh
        && !stream_transient_mesh(cmd_buf.device, state_->cached_grid_mesh, frame_resources.geometry_arena, grid_slice))
    {
        return;
    }

    MTLViewport viewport;
    viewport.originX = ctx.viewport_x();
    viewport.originY = ctx.viewport_y();
    viewport.width = ctx.viewport_w();
    viewport.height = ctx.viewport_h();
    viewport.znear = 0.0;
    viewport.zfar = 1.0;
    [encoder setViewport:viewport];

    MTLScissorRect scissor;
    scissor.x = static_cast<NSUInteger>(std::max(0, ctx.viewport_x()));
    scissor.y = static_cast<NSUInteger>(std::max(0, ctx.viewport_y()));
    scissor.width = static_cast<NSUInteger>(std::max(0, ctx.viewport_w()));
    scissor.height = static_cast<NSUInteger>(std::max(0, ctx.viewport_h()));
    [encoder setScissorRect:scissor];

    FrameUniforms frame;
    frame.view = to_simd_matrix(scene_.camera.view);
    frame.proj = to_simd_matrix(scene_.camera.proj);
    frame.light_dir = simd_make_float4(
        scene_.camera.light_dir.x,
        scene_.camera.light_dir.y,
        scene_.camera.light_dir.z,
        scene_.camera.light_dir.w);
    std::memcpy([frame_resources.frame_uniforms.get() contents], &frame, sizeof(frame));

    [encoder setRenderPipelineState:state_->pipeline.get()];
    [encoder setDepthStencilState:state_->depth_state.get()];
    [encoder setCullMode:MTLCullModeBack];
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    [encoder setVertexBuffer:frame_resources.frame_uniforms.get() offset:0 atIndex:1];

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

        ObjectUniforms object;
        object.world = to_simd_matrix(obj.world);
        object.color = simd_make_float4(obj.color.x, obj.color.y, obj.color.z, obj.color.w);

        [encoder setVertexBuffer:mesh->vertex_buffer.get() offset:0 atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:mesh->index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:mesh->index_buffer.get()
                     indexBufferOffset:0];
    }

    if (grid_slice.index_count > 0)
    {
        ObjectUniforms object;
        object.world = matrix_identity_float4x4;
        object.color = simd_make_float4(
            scene_.floor_grid.color.x,
            scene_.floor_grid.color.y,
            scene_.floor_grid.color.z,
            scene_.floor_grid.color.w);

        [encoder setVertexBuffer:grid_slice.vertex_buffer offset:grid_slice.vertex_offset atIndex:0];
        [encoder setVertexBytes:&object length:sizeof(object) atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:grid_slice.index_count
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:grid_slice.index_buffer
                     indexBufferOffset:grid_slice.index_offset];
    }
}

} // namespace draxul
