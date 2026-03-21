#pragma once
#include <draxul/base_renderer.h>
#include <draxul/types.h>
#include <memory>
#include <optional>
#include <span>
#include <utility>

struct ImDrawData;

namespace draxul
{

class IWindow;

// ---------------------------------------------------------------------------
// IGridRenderer — grid rendering contract, extends I3DRenderer.
// The renderer hierarchy is: IBaseRenderer → I3DRenderer → IGridRenderer.
// Concrete backends (MetalRenderer, VkRenderer) implement IGridRenderer,
// which transitively satisfies both I3DRenderer and IBaseRenderer.
// ---------------------------------------------------------------------------
class IGridRenderer : public I3DRenderer
{
public:
    ~IGridRenderer() override = default;
    virtual void set_grid_size(int cols, int rows) = 0;
    virtual void update_cells(std::span<const CellUpdate> updates) = 0;
    virtual void set_overlay_cells(std::span<const CellUpdate> updates) = 0;
    virtual void set_atlas_texture(const uint8_t* data, int w, int h) = 0;
    virtual void update_atlas_region(int x, int y, int w, int h, const uint8_t* data) = 0;
    virtual void set_cursor(int col, int row, const CursorStyle& style) = 0;
    virtual std::pair<int, int> cell_size_pixels() const = 0;
    virtual void set_cell_size(int w, int h) = 0;
    virtual void set_ascender(int a) = 0;
    virtual int padding() const = 0;
    virtual void set_scroll_offset(float px) = 0;
};

// ---------------------------------------------------------------------------
// IImGuiHost — ImGui backend lifecycle
// Used by App for the debug panel. Cast from IGridRenderer* when needed.
// ---------------------------------------------------------------------------
class IImGuiHost
{
public:
    virtual ~IImGuiHost() = default;
    virtual bool initialize_imgui_backend() = 0;
    virtual void shutdown_imgui_backend() = 0;
    virtual void rebuild_imgui_font_texture() = 0;
    virtual void begin_imgui_frame() = 0;
    virtual void set_imgui_draw_data(const ImDrawData* draw_data) = 0;
};

// ---------------------------------------------------------------------------
// ICaptureRenderer — frame capture for CI / render-test builds only
// Cast from IGridRenderer* when frame capture is needed.
// ---------------------------------------------------------------------------
class ICaptureRenderer
{
public:
    virtual ~ICaptureRenderer() = default;
    virtual void request_frame_capture() = 0;
    virtual std::optional<CapturedFrame> take_captured_frame() = 0;
};

// ---------------------------------------------------------------------------
// IRenderer — backward-compat combined interface
// Concrete backends (MetalRenderer, VkRenderer) inherit IRenderer so they
// satisfy all three focused interfaces through a single base pointer.
// Tests that define FakeRenderer : public IRenderer continue to work.
// App code should use RendererBundle (returned by create_renderer) instead
// of holding IRenderer* directly.
// ---------------------------------------------------------------------------
class IRenderer
    : public IGridRenderer,
      public IImGuiHost,
      public ICaptureRenderer
{
public:
    ~IRenderer() override = default;
};

// ---------------------------------------------------------------------------
// RendererBundle — owning wrapper returned by create_renderer().
// Holds the concrete renderer and exposes typed interface pointers so App
// code never needs to cast or store IRenderer* directly.
// ---------------------------------------------------------------------------
struct RendererBundle
{
    std::unique_ptr<IRenderer> impl;

    IGridRenderer* grid() const
    {
        return impl.get();
    }
    IImGuiHost* imgui() const
    {
        return impl.get();
    }
    ICaptureRenderer* capture() const
    {
        return impl.get();
    }
    // IGridRenderer IS-A I3DRenderer — upcast is always valid and free.
    I3DRenderer* threed() const
    {
        return impl.get();
    }

    explicit operator bool() const
    {
        return impl != nullptr;
    }
};

RendererBundle create_renderer(int atlas_size = kAtlasSize);

} // namespace draxul
