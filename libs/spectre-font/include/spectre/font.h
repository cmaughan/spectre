#pragma once
#include <spectre/font_metrics.h>
#include <spectre/types.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>

namespace spectre {

class FontManager {
public:
    bool initialize(const std::string& font_path, int point_size, float display_ppi);
    bool set_point_size(int point_size);
    void shutdown();

    FT_Face face() const { return face_; }
    hb_font_t* hb_font() const { return hb_font_; }
    const FontMetrics& metrics() const { return metrics_; }
    int point_size() const { return point_size_; }

private:
    FT_Library ft_lib_ = nullptr;
    FT_Face face_ = nullptr;
    hb_font_t* hb_font_ = nullptr;
    void update_metrics();

    FontMetrics metrics_ = {};
    int point_size_ = 0;
    float display_ppi_ = 96.0f;
};

class GlyphCache {
public:
    static constexpr int ATLAS_SIZE = 2048;

    bool initialize(FT_Face face, int pixel_size);
    void reset(FT_Face face, int pixel_size);

    const AtlasRegion& get_glyph(uint32_t glyph_id);

    bool atlas_dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    const uint8_t* atlas_data() const { return atlas_.data(); }
    int atlas_width() const { return ATLAS_SIZE; }
    int atlas_height() const { return ATLAS_SIZE; }

    struct DirtyRect { int x, y, w, h; };
    const DirtyRect& dirty_rect() const { return dirty_rect_; }

private:
    bool rasterize_glyph(uint32_t glyph_id, AtlasRegion& region);

    FT_Face face_ = nullptr;
    int pixel_size_ = 0;

    std::vector<uint8_t> atlas_;
    std::unordered_map<uint32_t, AtlasRegion> cache_;

    int shelf_x_ = 0;
    int shelf_y_ = 0;
    int shelf_height_ = 0;

    bool dirty_ = false;
    DirtyRect dirty_rect_ = {};

    AtlasRegion empty_region_ = {};
};

struct ShapedGlyph {
    uint32_t glyph_id;
    int x_advance;
    int x_offset;
    int y_offset;
    int cluster;
};

class TextShaper {
public:
    void initialize(hb_font_t* font);

    std::vector<ShapedGlyph> shape(const std::string& text);
    uint32_t shape_codepoint(uint32_t codepoint);

private:
    hb_font_t* font_ = nullptr;
    hb_buffer_t* buffer_ = nullptr;
};

} // namespace spectre
