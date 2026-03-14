#pragma once
#include <spectre/renderer.h>
#include <vector>

// Forward declarations for Objective-C types
#ifdef __OBJC__
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLRenderPipelineState;
@protocol MTLBuffer;
@protocol MTLTexture;
@protocol MTLSamplerState;
@class CAMetalLayer;
#else
typedef void* id;
#endif

namespace spectre
{

class MetalRenderer : public IRenderer
{
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr int ATLAS_SIZE = 2048;

    bool initialize(IWindow& window) override;
    void shutdown() override;
    bool begin_frame() override;
    void end_frame() override;
    void set_grid_size(int cols, int rows) override;
    void update_cells(std::span<const CellUpdate> updates) override;
    void set_atlas_texture(const uint8_t* data, int w, int h) override;
    void update_atlas_region(int x, int y, int w, int h, const uint8_t* data) override;
    void set_cursor(int col, int row, const CursorStyle& style) override;
    void resize(int pixel_w, int pixel_h) override;
    std::pair<int, int> cell_size_pixels() const override;
    void set_cell_size(int w, int h) override;
    void set_ascender(int a) override;
    int padding() const override
    {
        return padding_;
    }

private:
    void apply_cursor();
    void restore_cursor();

    // Opaque pointers to Metal objects (avoid ObjC in header)
    void* device_ = nullptr; // id<MTLDevice>
    void* command_queue_ = nullptr; // id<MTLCommandQueue>
    void* layer_ = nullptr; // CAMetalLayer*
    void* bg_pipeline_ = nullptr; // id<MTLRenderPipelineState>
    void* fg_pipeline_ = nullptr; // id<MTLRenderPipelineState>
    void* grid_buffer_ = nullptr; // id<MTLBuffer>
    void* atlas_texture_ = nullptr; // id<MTLTexture>
    void* atlas_sampler_ = nullptr; // id<MTLSamplerState>
    void* frame_semaphore_ = nullptr; // dispatch_semaphore_t

    int grid_cols_ = 0;
    int grid_rows_ = 0;
    int cell_w_ = 10;
    int cell_h_ = 20;
    int ascender_ = 16;
    int padding_ = 1;
    int pixel_w_ = 0;
    int pixel_h_ = 0;

    // Cursor
    int cursor_col_ = 0, cursor_row_ = 0;
    CursorStyle cursor_style_ = {};

    std::vector<GpuCell> gpu_cells_;
    GpuCell cursor_saved_cell_ = {};
    bool cursor_applied_ = false;
    bool cursor_overlay_active_ = false;

    // Current drawable for the frame
    void* current_drawable_ = nullptr; // id<CAMetalDrawable>
};

} // namespace spectre
