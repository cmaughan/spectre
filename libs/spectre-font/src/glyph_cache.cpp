#include <spectre/font.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace spectre {

bool GlyphCache::initialize(FT_Face face, int pixel_size) {
    face_ = face;
    pixel_size_ = pixel_size;
    atlas_.resize(ATLAS_SIZE * ATLAS_SIZE, 0);
    return true;
}

void GlyphCache::reset(FT_Face face, int pixel_size) {
    face_ = face;
    pixel_size_ = pixel_size;
    cache_.clear();
    std::fill(atlas_.begin(), atlas_.end(), (uint8_t)0);
    shelf_x_ = 0;
    shelf_y_ = 0;
    shelf_height_ = 0;
    dirty_ = true;
}

const AtlasRegion& GlyphCache::get_glyph(uint32_t glyph_id) {
    auto it = cache_.find(glyph_id);
    if (it != cache_.end()) return it->second;

    AtlasRegion region = {};
    if (rasterize_glyph(glyph_id, region)) {
        auto [ins, _] = cache_.emplace(glyph_id, region);
        return ins->second;
    }
    return empty_region_;
}

bool GlyphCache::rasterize_glyph(uint32_t glyph_id, AtlasRegion& region) {
    if (FT_Load_Glyph(face_, glyph_id, FT_LOAD_DEFAULT)) {
        return false;
    }

    if (FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL)) {
        return false;
    }

    FT_Bitmap& bmp = face_->glyph->bitmap;
    int w = (int)bmp.width;
    int h = (int)bmp.rows;

    if (w == 0 || h == 0) {
        region = {};
        return true;
    }

    if (shelf_x_ + w + 1 > ATLAS_SIZE) {
        shelf_y_ += shelf_height_ + 1;
        shelf_x_ = 0;
        shelf_height_ = 0;
    }

    if (shelf_y_ + h > ATLAS_SIZE) {
        fprintf(stderr, "Atlas full! Cannot fit glyph %u (%dx%d)\n", glyph_id, w, h);
        return false;
    }

    for (int row = 0; row < h; row++) {
        memcpy(
            atlas_.data() + (shelf_y_ + row) * ATLAS_SIZE + shelf_x_,
            bmp.buffer + row * bmp.pitch,
            w
        );
    }

    float inv_size = 1.0f / ATLAS_SIZE;
    region.u0 = shelf_x_ * inv_size;
    region.v0 = shelf_y_ * inv_size;
    region.u1 = (shelf_x_ + w) * inv_size;
    region.v1 = (shelf_y_ + h) * inv_size;
    region.bearing_x = face_->glyph->bitmap_left;
    region.bearing_y = face_->glyph->bitmap_top;
    region.width = w;
    region.height = h;

    dirty_rect_ = { shelf_x_, shelf_y_, w, h };
    dirty_ = true;

    shelf_x_ += w + 1;
    shelf_height_ = std::max(shelf_height_, h);

    return true;
}

} // namespace spectre
