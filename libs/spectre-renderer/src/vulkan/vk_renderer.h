#pragma once
#include <spectre/renderer.h>
#include "vk_context.h"
#include "vk_pipeline.h"
#include "vk_buffers.h"
#include "vk_atlas.h"
#include <vector>

namespace spectre {

class VkRenderer : public IRenderer {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    bool initialize(IWindow& window) override;
    void shutdown() override;
    bool begin_frame() override;
    void end_frame() override;
    void set_grid_size(int cols, int rows) override;
    void update_cells(std::span<const CellUpdate> updates) override;
    void set_atlas_texture(const uint8_t* data, int w, int h) override;
    void update_atlas_region(int x, int y, int w, int h, const uint8_t* data) override;
    void set_cursor(int col, int row, CursorShape shape, Color color) override;
    void resize(int pixel_w, int pixel_h) override;
    std::pair<int,int> cell_size_pixels() const override;
    void set_cell_size(int w, int h) override;
    void set_ascender(int a) override;
    int padding() const override { return padding_; }

private:
    bool create_sync_objects();
    bool create_command_buffers();
    void create_descriptor_pool();
    void update_descriptor_sets_for_frame(int frame);
    void update_all_descriptor_sets();
    void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);
    void apply_cursor();
    void restore_cursor();

    VkContext ctx_;
    VkPipelineManager pipeline_;
    VkAtlas atlas_;
    VkGridBuffer grid_buffer_;

    // Per-frame sync
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd_buffers_;
    std::vector<VkSemaphore> image_available_sem_;
    std::vector<VkSemaphore> render_finished_sem_;
    std::vector<VkFence> in_flight_fences_;

    // Descriptors
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> bg_desc_sets_;
    std::vector<VkDescriptorSet> fg_desc_sets_;

    uint32_t current_frame_ = 0;
    uint32_t current_image_ = 0;
    bool framebuffer_resized_ = false;

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
    CursorShape cursor_shape_ = CursorShape::Block;
    Color cursor_color_ = { 1, 1, 1, 0.7f };

    std::vector<GpuCell> gpu_cells_;
    bool needs_descriptor_update_ = true;
    uint32_t desc_update_pending_frames_ = 0;
    GpuCell cursor_saved_cell_ = {};
    bool cursor_applied_ = false;
    bool cursor_overlay_active_ = false;
};

} // namespace spectre
