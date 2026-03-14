#pragma once
#include <cstdint>
#include <spectre/font_metrics.h>
#include <spectre/types.h>
#include <string>
#include <unordered_map>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>

namespace spectre
{

class TextShaper;

class FontManager
{
public:
    FontManager() = default;
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    FontManager(FontManager&& other) noexcept;
    FontManager& operator=(FontManager&& other) noexcept;

    bool initialize(const std::string& font_path, int point_size, float display_ppi);
    bool set_point_size(int point_size);
    void shutdown();

    FT_Face face() const
    {
        return face_;
    }
    hb_font_t* hb_font() const
    {
        return hb_font_;
    }
    const FontMetrics& metrics() const
    {
        return metrics_;
    }
    int point_size() const
    {
        return point_size_;
    }

private:
    FT_Library ft_lib_ = nullptr;
    FT_Face face_ = nullptr;
    hb_font_t* hb_font_ = nullptr;
    void update_metrics();

    FontMetrics metrics_ = {};
    int point_size_ = 0;
    float display_ppi_ = 96.0f;
};

class GlyphCache
{
public:
    static constexpr int ATLAS_SIZE = 2048;

    bool initialize(FT_Face face, int pixel_size);
    void reset(FT_Face face, int pixel_size);

    const AtlasRegion& get_glyph(uint32_t glyph_id);
    const AtlasRegion& get_cluster(const std::string& text, FT_Face face, TextShaper& shaper);

    bool atlas_dirty() const
    {
        return dirty_;
    }
    void clear_dirty()
    {
        dirty_ = false;
    }

    const uint8_t* atlas_data() const
    {
        return atlas_.data();
    }
    int atlas_width() const
    {
        return ATLAS_SIZE;
    }
    int atlas_height() const
    {
        return ATLAS_SIZE;
    }

    struct DirtyRect
    {
        int x, y, w, h;
    };
    const DirtyRect& dirty_rect() const
    {
        return dirty_rect_;
    }

private:
    struct ClusterKey
    {
        FT_Face face = nullptr;
        std::string text;

        bool operator==(const ClusterKey&) const = default;
    };

    struct ClusterKeyHash
    {
        size_t operator()(const ClusterKey& key) const;
    };

    bool rasterize_glyph(uint32_t glyph_id, AtlasRegion& region);
    bool rasterize_cluster(const std::string& text, FT_Face face, TextShaper& shaper, AtlasRegion& region);
    bool reserve_region(int w, int h, int& atlas_x, int& atlas_y, const char* label);

    FT_Face face_ = nullptr;
    int pixel_size_ = 0;

    std::vector<uint8_t> atlas_;
    std::unordered_map<uint32_t, AtlasRegion> glyph_cache_;
    std::unordered_map<ClusterKey, AtlasRegion, ClusterKeyHash> cluster_cache_;

    int shelf_x_ = 0;
    int shelf_y_ = 0;
    int shelf_height_ = 0;

    bool dirty_ = false;
    DirtyRect dirty_rect_ = {};

    AtlasRegion empty_region_ = {};
};

struct ShapedGlyph
{
    uint32_t glyph_id;
    int x_advance;
    int x_offset;
    int y_offset;
    int cluster;
};

class TextShaper
{
public:
    TextShaper() = default;
    TextShaper(const TextShaper&) = delete;
    TextShaper& operator=(const TextShaper&) = delete;
    TextShaper(TextShaper&& other) noexcept;
    TextShaper& operator=(TextShaper&& other) noexcept;
    ~TextShaper();

    void initialize(hb_font_t* font);
    void shutdown();

    std::vector<ShapedGlyph> shape(const std::string& text);
    uint32_t shape_codepoint(uint32_t codepoint);

private:
    hb_font_t* font_ = nullptr;
    hb_buffer_t* buffer_ = nullptr;
};

} // namespace spectre
