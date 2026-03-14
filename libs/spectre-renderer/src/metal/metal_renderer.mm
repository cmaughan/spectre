#include "metal_renderer.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <spectre/window.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace spectre
{

bool MetalRenderer::initialize(IWindow& window)
{
    // Get Metal device
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
    {
        fprintf(stderr, "[metal] Failed to create Metal device\n");
        return false;
    }
    device_ = (__bridge_retained void*)device;

    // Get CAMetalLayer from SDL window
    SDL_MetalView metalView = SDL_Metal_CreateView(window.native_handle());
    if (!metalView)
    {
        fprintf(stderr, "[metal] Failed to create Metal view: %s\n", SDL_GetError());
        return false;
    }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metalView);
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer_ = (__bridge_retained void*)layer;

    auto [w, h] = window.size_pixels();
    pixel_w_ = w;
    pixel_h_ = h;
    layer.drawableSize = CGSizeMake(w, h);

    // Create command queue
    id<MTLCommandQueue> queue = [device newCommandQueue];
    command_queue_ = (__bridge_retained void*)queue;

    // Load shader library from metallib
    NSError* error = nil;
    NSURL* libURL = [[NSBundle mainBundle] URLForResource:@"grid" withExtension:@"metallib"];
    id<MTLLibrary> library = nil;
    if (libURL)
    {
        library = [device newLibraryWithURL:libURL error:&error];
    }
    if (!library)
    {
        // Try loading from shaders/ directory next to executable
        NSString* exePath = [[NSBundle mainBundle] executablePath];
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        NSString* shaderPath = [exeDir stringByAppendingPathComponent:@"shaders/grid.metallib"];
        NSURL* shaderURL = [NSURL fileURLWithPath:shaderPath];
        library = [device newLibraryWithURL:shaderURL error:&error];
    }
    if (!library)
    {
        fprintf(stderr, "[metal] Failed to load shader library: %s\n",
            error ? [[error localizedDescription] UTF8String] : "unknown");
        return false;
    }

    // Create background pipeline
    {
        id<MTLFunction> vertFunc = [library newFunctionWithName:@"bg_vertex"];
        id<MTLFunction> fragFunc = [library newFunctionWithName:@"bg_fragment"];
        if (!vertFunc || !fragFunc)
        {
            fprintf(stderr, "[metal] Failed to find bg shader functions\n");
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertFunc;
        desc.fragmentFunction = fragFunc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        // BG pass: no blending (opaque)
        desc.colorAttachments[0].blendingEnabled = NO;

        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
        {
            fprintf(stderr, "[metal] Failed to create bg pipeline: %s\n",
                [[error localizedDescription] UTF8String]);
            return false;
        }
        bg_pipeline_ = (__bridge_retained void*)pipeline;
    }

    // Create foreground pipeline
    {
        id<MTLFunction> vertFunc = [library newFunctionWithName:@"fg_vertex"];
        id<MTLFunction> fragFunc = [library newFunctionWithName:@"fg_fragment"];
        if (!vertFunc || !fragFunc)
        {
            fprintf(stderr, "[metal] Failed to find fg shader functions\n");
            return false;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertFunc;
        desc.fragmentFunction = fragFunc;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        // FG pass: alpha blending
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

        id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
        {
            fprintf(stderr, "[metal] Failed to create fg pipeline: %s\n",
                [[error localizedDescription] UTF8String]);
            return false;
        }
        fg_pipeline_ = (__bridge_retained void*)pipeline;
    }

    // Create atlas texture (R8 -> we use .r component in shader)
    {
        MTLTextureDescriptor* texDesc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                         width:ATLAS_SIZE
                                        height:ATLAS_SIZE
                                     mipmapped:NO];
        texDesc.usage = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;

        id<MTLTexture> texture = [device newTextureWithDescriptor:texDesc];
        atlas_texture_ = (__bridge_retained void*)texture;
    }

    // Create atlas sampler
    {
        MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
        sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;

        id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sampDesc];
        atlas_sampler_ = (__bridge_retained void*)sampler;
    }

    // Create grid buffer (start with 80x24)
    {
        size_t initial_size = 80 * 24 * sizeof(GpuCell);
        id<MTLBuffer> buffer = [device newBufferWithLength:initial_size
                                                   options:MTLResourceStorageModeShared];
        grid_buffer_ = (__bridge_retained void*)buffer;
    }

    // Create frame semaphore for double buffering
    // Use count of 1 since we have a single shared grid buffer —
    // must wait for GPU to finish reading before CPU modifies it
    frame_semaphore_ = (__bridge_retained void*)dispatch_semaphore_create(1);

    fprintf(stderr, "[metal] Renderer initialized (%s)\n", [[device name] UTF8String]);
    return true;
}

void MetalRenderer::shutdown()
{
    // Wait for the in-flight frame to complete
    dispatch_semaphore_t sema = (__bridge dispatch_semaphore_t)frame_semaphore_;
    if (sema)
    {
        dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
        dispatch_semaphore_signal(sema);
    }

    // Release Metal objects by transferring ownership back to ARC
    auto release = [](void*& p) {
        if (p)
        {
            (void)(__bridge_transfer id)p;
            p = nullptr;
        }
    };
    release(atlas_sampler_);
    release(atlas_texture_);
    release(grid_buffer_);
    release(fg_pipeline_);
    release(bg_pipeline_);
    release(command_queue_);
    release(layer_);
    release(device_);
}

void MetalRenderer::set_grid_size(int cols, int rows)
{
    grid_cols_ = cols;
    grid_rows_ = rows;
    cursor_applied_ = false;
    cursor_overlay_active_ = false;

    // +1 for cursor overlay cell
    size_t required = ((size_t)cols * rows + 1) * sizeof(GpuCell);

    id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
    id<MTLBuffer> existing = (__bridge id<MTLBuffer>)grid_buffer_;

    if (!existing || [existing length] < required)
    {
        if (grid_buffer_)
        {
            (void)(__bridge_transfer id)grid_buffer_;
            grid_buffer_ = nullptr;
        }
        id<MTLBuffer> newBuf = [device newBufferWithLength:required
                                                   options:MTLResourceStorageModeShared];
        grid_buffer_ = (__bridge_retained void*)newBuf;
    }

    gpu_cells_.resize(cols * rows);
    memset(gpu_cells_.data(), 0, gpu_cells_.size() * sizeof(GpuCell));

    // Initialize cell positions
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

    // Upload to GPU
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)grid_buffer_;
    memcpy([buf contents], gpu_cells_.data(), gpu_cells_.size() * sizeof(GpuCell));
}

void MetalRenderer::update_cells(std::span<const CellUpdate> updates)
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

    // Upload to GPU
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)grid_buffer_;
    memcpy([buf contents], gpu_cells_.data(), gpu_cells_.size() * sizeof(GpuCell));
}

void MetalRenderer::set_atlas_texture(const uint8_t* data, int w, int h)
{
    id<MTLTexture> tex = (__bridge id<MTLTexture>)atlas_texture_;
    MTLRegion region = MTLRegionMake2D(0, 0, w, h);
    [tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:w];
}

void MetalRenderer::update_atlas_region(int x, int y, int w, int h, const uint8_t* data)
{
    id<MTLTexture> tex = (__bridge id<MTLTexture>)atlas_texture_;
    MTLRegion region = MTLRegionMake2D(x, y, w, h);
    [tex replaceRegion:region mipmapLevel:0 withBytes:data bytesPerRow:w];
}

void MetalRenderer::set_cursor(int col, int row, const CursorStyle& style)
{
    restore_cursor();

    cursor_col_ = col;
    cursor_row_ = row;
    cursor_style_ = style;
}

void MetalRenderer::resize(int pixel_w, int pixel_h)
{
    pixel_w_ = pixel_w;
    pixel_h_ = pixel_h;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_;
    layer.drawableSize = CGSizeMake(pixel_w, pixel_h);
}

std::pair<int, int> MetalRenderer::cell_size_pixels() const
{
    return { cell_w_, cell_h_ };
}

void MetalRenderer::set_cell_size(int w, int h)
{
    cell_w_ = w;
    cell_h_ = h;
}

void MetalRenderer::set_ascender(int a)
{
    ascender_ = a;
}

void MetalRenderer::apply_cursor()
{
    if (cursor_col_ < 0 || cursor_col_ >= grid_cols_ || cursor_row_ < 0 || cursor_row_ >= grid_rows_)
        return;

    int idx = cursor_row_ * grid_cols_ + cursor_col_;
    auto& cell = gpu_cells_[idx];

    cursor_saved_cell_ = cell;
    cursor_applied_ = true;
    cursor_overlay_active_ = false;

    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)grid_buffer_;

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

        memcpy((char*)[buf contents] + idx * sizeof(GpuCell), &cell, sizeof(GpuCell));
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

        memcpy((char*)[buf contents] + overlay_idx * sizeof(GpuCell),
            &overlay, sizeof(GpuCell));
        cursor_overlay_active_ = true;
    }
}

void MetalRenderer::restore_cursor()
{
    if (!cursor_applied_)
        return;
    cursor_applied_ = false;
    cursor_overlay_active_ = false;

    if (cursor_col_ < 0 || cursor_col_ >= grid_cols_ || cursor_row_ < 0 || cursor_row_ >= grid_rows_)
        return;

    int idx = cursor_row_ * grid_cols_ + cursor_col_;
    gpu_cells_[idx] = cursor_saved_cell_;

    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)grid_buffer_;
    memcpy((char*)[buf contents] + idx * sizeof(GpuCell),
        &cursor_saved_cell_, sizeof(GpuCell));
}

bool MetalRenderer::begin_frame()
{
    dispatch_semaphore_t sema = (__bridge dispatch_semaphore_t)frame_semaphore_;
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    restore_cursor();

    CAMetalLayer* layer = (__bridge CAMetalLayer*)layer_;
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable)
    {
        dispatch_semaphore_signal(sema);
        return false;
    }
    current_drawable_ = (__bridge_retained void*)drawable;

    return true;
}

void MetalRenderer::end_frame()
{
    apply_cursor();

    id<CAMetalDrawable> drawable = (__bridge_transfer id<CAMetalDrawable>)current_drawable_;
    current_drawable_ = nullptr;

    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
    id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];

    // Create render pass descriptor
    MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
    rpDesc.colorAttachments[0].texture = drawable.texture;
    rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
    rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
    rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.1, 0.1, 0.1, 1.0);

    id<MTLRenderCommandEncoder> encoder = [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];

    int total_cells = grid_cols_ * grid_rows_;
    int bg_instances = total_cells + (cursor_overlay_active_ ? 1 : 0);

    if (total_cells > 0)
    {
        // Push constants
        struct
        {
            float screen_w, screen_h, cell_w, cell_h;
        } push_data = {
            (float)pixel_w_, (float)pixel_h_,
            (float)cell_w_, (float)cell_h_
        };

        id<MTLBuffer> gridBuf = (__bridge id<MTLBuffer>)grid_buffer_;
        id<MTLTexture> atlasTex = (__bridge id<MTLTexture>)atlas_texture_;
        id<MTLSamplerState> sampler = (__bridge id<MTLSamplerState>)atlas_sampler_;

        // Background pass
        [encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)bg_pipeline_];
        [encoder setVertexBuffer:gridBuf offset:0 atIndex:0];
        [encoder setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:bg_instances];

        // Foreground pass
        [encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)fg_pipeline_];
        [encoder setVertexBuffer:gridBuf offset:0 atIndex:0];
        [encoder setVertexBytes:&push_data length:sizeof(push_data) atIndex:1];
        [encoder setFragmentTexture:atlasTex atIndex:0];
        [encoder setFragmentSamplerState:sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:6
                  instanceCount:total_cells];
    }

    [encoder endEncoding];

    [cmdBuf presentDrawable:drawable];

    // Signal semaphore when GPU is done
    dispatch_semaphore_t sema = (__bridge dispatch_semaphore_t)frame_semaphore_;
    [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer>) {
        dispatch_semaphore_signal(sema);
    }];

    [cmdBuf commit];
}

} // namespace spectre
