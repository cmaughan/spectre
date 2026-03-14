#include <ft2build.h>
#include <spectre/font.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>

namespace spectre
{

namespace
{

struct RasterizedGlyph
{
    int width = 0;
    int height = 0;
    int left = 0;
    int top = 0;
    std::vector<uint8_t> pixels;
};

const uint8_t* bitmap_row_ptr(const FT_Bitmap& bmp, int row)
{
    if (bmp.pitch >= 0)
        return bmp.buffer + row * bmp.pitch;
    return bmp.buffer + (bmp.rows - 1 - row) * (-bmp.pitch);
}

bool bitmap_to_grayscale(const FT_Bitmap& bmp, std::vector<uint8_t>& out)
{
    int width = (int)bmp.width;
    int height = (int)bmp.rows;
    if (width <= 0 || height <= 0)
    {
        out.clear();
        return true;
    }

    out.assign((size_t)width * height, 0);

    switch (bmp.pixel_mode)
    {
    case FT_PIXEL_MODE_GRAY:
        for (int row = 0; row < height; row++)
        {
            const uint8_t* src = bitmap_row_ptr(bmp, row);
            memcpy(out.data() + (size_t)row * width, src, width);
        }
        return true;

    case FT_PIXEL_MODE_MONO:
        for (int row = 0; row < height; row++)
        {
            const uint8_t* src = bitmap_row_ptr(bmp, row);
            for (int col = 0; col < width; col++)
            {
                uint8_t mask = (uint8_t)(0x80 >> (col & 7));
                out[(size_t)row * width + col] = (src[col >> 3] & mask) ? 255 : 0;
            }
        }
        return true;

    case FT_PIXEL_MODE_BGRA:
        for (int row = 0; row < height; row++)
        {
            const uint8_t* src = bitmap_row_ptr(bmp, row);
            for (int col = 0; col < width; col++)
            {
                out[(size_t)row * width + col] = src[col * 4 + 3];
            }
        }
        return true;
    }

    fprintf(stderr, "Unsupported FreeType pixel mode: %u\n", bmp.pixel_mode);
    return false;
}

} // namespace

bool GlyphCache::initialize(FT_Face face, int pixel_size)
{
    face_ = face;
    pixel_size_ = pixel_size;
    atlas_.resize(ATLAS_SIZE * ATLAS_SIZE, 0);
    return true;
}

void GlyphCache::reset(FT_Face face, int pixel_size)
{
    face_ = face;
    pixel_size_ = pixel_size;
    glyph_cache_.clear();
    cluster_cache_.clear();
    std::fill(atlas_.begin(), atlas_.end(), (uint8_t)0);
    shelf_x_ = 0;
    shelf_y_ = 0;
    shelf_height_ = 0;
    dirty_ = true;
}

size_t GlyphCache::ClusterKeyHash::operator()(const ClusterKey& key) const
{
    size_t h1 = std::hash<FT_Face>()(key.face);
    size_t h2 = std::hash<std::string>()(key.text);
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

const AtlasRegion& GlyphCache::get_glyph(uint32_t glyph_id)
{
    auto it = glyph_cache_.find(glyph_id);
    if (it != glyph_cache_.end())
        return it->second;

    AtlasRegion region = {};
    if (rasterize_glyph(glyph_id, region))
    {
        auto [ins, _] = glyph_cache_.emplace(glyph_id, region);
        return ins->second;
    }
    return empty_region_;
}

const AtlasRegion& GlyphCache::get_cluster(const std::string& text, FT_Face face, TextShaper& shaper)
{
    if (text.empty() || text == " ")
        return empty_region_;

    ClusterKey key = { face, text };
    auto it = cluster_cache_.find(key);
    if (it != cluster_cache_.end())
        return it->second;

    AtlasRegion region = {};
    if (rasterize_cluster(text, face, shaper, region))
    {
        auto [ins, _] = cluster_cache_.emplace(std::move(key), region);
        return ins->second;
    }

    return empty_region_;
}

bool GlyphCache::reserve_region(int w, int h, int& atlas_x, int& atlas_y, const char* label)
{
    if (w <= 0 || h <= 0)
        return false;

    if (shelf_x_ + w + 1 > ATLAS_SIZE)
    {
        shelf_y_ += shelf_height_ + 1;
        shelf_x_ = 0;
        shelf_height_ = 0;
    }

    if (shelf_y_ + h > ATLAS_SIZE)
    {
        fprintf(stderr, "Atlas full! Cannot fit %s (%dx%d)\n", label, w, h);
        return false;
    }

    atlas_x = shelf_x_;
    atlas_y = shelf_y_;
    shelf_x_ += w + 1;
    shelf_height_ = std::max(shelf_height_, h);
    return true;
}

bool GlyphCache::rasterize_glyph(uint32_t glyph_id, AtlasRegion& region)
{
    if (FT_Load_Glyph(face_, glyph_id, FT_LOAD_DEFAULT | FT_LOAD_COLOR))
    {
        return false;
    }

    if (face_->glyph->format != FT_GLYPH_FORMAT_BITMAP
        && FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL))
    {
        return false;
    }

    FT_Bitmap& bmp = face_->glyph->bitmap;
    int w = (int)bmp.width;
    int h = (int)bmp.rows;

    if (w == 0 || h == 0)
    {
        region = {};
        return true;
    }

    int atlas_x = 0;
    int atlas_y = 0;
    if (!reserve_region(w, h, atlas_x, atlas_y, "glyph"))
    {
        return false;
    }

    std::vector<uint8_t> pixels;
    if (!bitmap_to_grayscale(bmp, pixels))
        return false;

    for (int row = 0; row < h; row++)
    {
        memcpy(
            atlas_.data() + (atlas_y + row) * ATLAS_SIZE + atlas_x,
            pixels.data() + (size_t)row * w,
            w);
    }

    float inv_size = 1.0f / ATLAS_SIZE;
    region.u0 = atlas_x * inv_size;
    region.v0 = atlas_y * inv_size;
    region.u1 = (atlas_x + w) * inv_size;
    region.v1 = (atlas_y + h) * inv_size;
    region.bearing_x = face_->glyph->bitmap_left;
    region.bearing_y = face_->glyph->bitmap_top;
    region.width = w;
    region.height = h;

    dirty_rect_ = { atlas_x, atlas_y, w, h };
    dirty_ = true;

    return true;
}

bool GlyphCache::rasterize_cluster(const std::string& text, FT_Face face, TextShaper& shaper, AtlasRegion& region)
{
    auto shaped = shaper.shape(text);
    if (shaped.empty())
    {
        region = {};
        return true;
    }

    std::vector<RasterizedGlyph> glyphs;
    glyphs.reserve(shaped.size());

    int pen_x = 0;
    int bbox_left = INT_MAX;
    int bbox_right = INT_MIN;
    int bbox_top = INT_MIN;
    int bbox_bottom = INT_MAX;

    for (const auto& shaped_glyph : shaped)
    {
        if (FT_Load_Glyph(face, shaped_glyph.glyph_id, FT_LOAD_DEFAULT | FT_LOAD_COLOR))
            return false;
        if (face->glyph->format != FT_GLYPH_FORMAT_BITMAP
            && FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL))
            return false;

        FT_Bitmap& bmp = face->glyph->bitmap;
        if (bmp.width > 0 && bmp.rows > 0)
        {
            RasterizedGlyph glyph;
            glyph.width = (int)bmp.width;
            glyph.height = (int)bmp.rows;
            glyph.left = pen_x + shaped_glyph.x_offset + face->glyph->bitmap_left;
            glyph.top = shaped_glyph.y_offset + face->glyph->bitmap_top;
            if (!bitmap_to_grayscale(bmp, glyph.pixels))
                return false;

            bbox_left = std::min(bbox_left, glyph.left);
            bbox_right = std::max(bbox_right, glyph.left + glyph.width);
            bbox_top = std::max(bbox_top, glyph.top);
            bbox_bottom = std::min(bbox_bottom, glyph.top - glyph.height);
            glyphs.push_back(std::move(glyph));
        }

        pen_x += shaped_glyph.x_advance;
    }

    if (glyphs.empty())
    {
        region = {};
        return true;
    }

    int cluster_width = bbox_right - bbox_left;
    int cluster_height = bbox_top - bbox_bottom;

    int atlas_x = 0;
    int atlas_y = 0;
    if (!reserve_region(cluster_width, cluster_height, atlas_x, atlas_y, "cluster"))
        return false;

    std::vector<uint8_t> composite(cluster_width * cluster_height, 0);
    for (const auto& glyph : glyphs)
    {
        int dst_x = glyph.left - bbox_left;
        int dst_y = bbox_top - glyph.top;
        for (int row = 0; row < glyph.height; row++)
        {
            for (int col = 0; col < glyph.width; col++)
            {
                auto& dst = composite[(dst_y + row) * cluster_width + dst_x + col];
                dst = std::max(dst, glyph.pixels[row * glyph.width + col]);
            }
        }
    }

    for (int row = 0; row < cluster_height; row++)
    {
        memcpy(atlas_.data() + (atlas_y + row) * ATLAS_SIZE + atlas_x,
            composite.data() + row * cluster_width,
            cluster_width);
    }

    float inv_size = 1.0f / ATLAS_SIZE;
    region.u0 = atlas_x * inv_size;
    region.v0 = atlas_y * inv_size;
    region.u1 = (atlas_x + cluster_width) * inv_size;
    region.v1 = (atlas_y + cluster_height) * inv_size;
    region.bearing_x = bbox_left;
    region.bearing_y = bbox_top;
    region.width = cluster_width;
    region.height = cluster_height;

    dirty_rect_ = { atlas_x, atlas_y, cluster_width, cluster_height };
    dirty_ = true;
    return true;
}

} // namespace spectre
