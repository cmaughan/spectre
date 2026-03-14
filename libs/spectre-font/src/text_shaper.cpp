#include <spectre/font.h>
#include <hb-ft.h>

namespace spectre {

void TextShaper::initialize(hb_font_t* font) {
    font_ = font;
    buffer_ = hb_buffer_create();
}

std::vector<ShapedGlyph> TextShaper::shape(const std::string& text) {
    std::vector<ShapedGlyph> result;

    hb_buffer_reset(buffer_);
    hb_buffer_add_utf8(buffer_, text.c_str(), (int)text.size(), 0, (int)text.size());
    hb_buffer_guess_segment_properties(buffer_);

    hb_shape(font_, buffer_, nullptr, 0);

    unsigned int glyph_count;
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer_, &glyph_count);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer_, &glyph_count);

    result.reserve(glyph_count);
    for (unsigned int i = 0; i < glyph_count; i++) {
        result.push_back({
            info[i].codepoint,
            (int)(pos[i].x_advance >> 6),
            (int)(pos[i].x_offset >> 6),
            (int)(pos[i].y_offset >> 6),
            (int)info[i].cluster
        });
    }

    return result;
}

uint32_t TextShaper::shape_codepoint(uint32_t codepoint) {
    hb_buffer_reset(buffer_);
    hb_buffer_add_codepoints(buffer_, &codepoint, 1, 0, 1);
    hb_buffer_guess_segment_properties(buffer_);

    hb_shape(font_, buffer_, nullptr, 0);

    unsigned int glyph_count;
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer_, &glyph_count);
    if (glyph_count > 0) return info[0].codepoint;
    return 0;
}

} // namespace spectre
