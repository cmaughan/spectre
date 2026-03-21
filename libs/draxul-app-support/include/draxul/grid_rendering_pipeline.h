#pragma once

#include <draxul/glyph_atlas.h>
#include <draxul/grid.h>
#include <draxul/renderer.h>
#include <vector>

namespace draxul
{

class GridRenderingPipeline
{
public:
    GridRenderingPipeline(Grid& grid, HighlightTable& highlights, IGlyphAtlas& glyph_atlas);
    void set_renderer(IGridRenderer* renderer);
    void set_enable_ligatures(bool enable);
    void flush();
    void force_full_atlas_upload();

private:
    void upload_atlas();

    IGridRenderer* renderer_ = nullptr;
    Grid& grid_;
    HighlightTable& highlights_;
    IGlyphAtlas& glyph_atlas_;

    bool force_full_atlas_upload_ = true;
    bool enable_ligatures_ = true;
    std::vector<uint8_t> atlas_upload_scratch_;
};

} // namespace draxul
