#pragma once
#include <memory>
#include <span>
#include <spectre/types.h>
#include <utility>

namespace spectre
{

class IWindow;

class IRenderer
{
public:
    virtual ~IRenderer() = default;
    virtual bool initialize(IWindow& window) = 0;
    virtual void shutdown() = 0;
    virtual bool begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void set_grid_size(int cols, int rows) = 0;
    virtual void update_cells(std::span<const CellUpdate> updates) = 0;
    virtual void set_atlas_texture(const uint8_t* data, int w, int h) = 0;
    virtual void update_atlas_region(int x, int y, int w, int h, const uint8_t* data) = 0;
    virtual void set_cursor(int col, int row, const CursorStyle& style) = 0;
    virtual void resize(int pixel_w, int pixel_h) = 0;
    virtual std::pair<int, int> cell_size_pixels() const = 0;
    virtual void set_cell_size(int w, int h) = 0;
    virtual void set_ascender(int a) = 0;
    virtual int padding() const = 0;
};

std::unique_ptr<IRenderer> create_renderer();

} // namespace spectre
