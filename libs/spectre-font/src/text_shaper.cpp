#include <hb-ft.h>
#include <spectre/font.h>
#include <utility>

namespace spectre
{

TextShaper::TextShaper(TextShaper&& other) noexcept
{
    *this = std::move(other);
}

TextShaper& TextShaper::operator=(TextShaper&& other) noexcept
{
    if (this == &other)
        return *this;

    shutdown();

    font_ = other.font_;
    buffer_ = other.buffer_;

    other.font_ = nullptr;
    other.buffer_ = nullptr;

    return *this;
}

TextShaper::~TextShaper()
{
    shutdown();
}

void TextShaper::initialize(hb_font_t* font)
{
    shutdown();
    font_ = font;
    buffer_ = hb_buffer_create();
}

void TextShaper::shutdown()
{
    if (buffer_)
    {
        hb_buffer_destroy(buffer_);
        buffer_ = nullptr;
    }
}

std::vector<ShapedGlyph> TextShaper::shape(const std::string& text)
{
    std::vector<ShapedGlyph> result;
    if (!font_ || !buffer_)
        return result;

    hb_buffer_reset(buffer_);
    hb_buffer_add_utf8(buffer_, text.c_str(), (int)text.size(), 0, (int)text.size());
    hb_buffer_guess_segment_properties(buffer_);

    hb_shape(font_, buffer_, nullptr, 0);

    unsigned int glyph_count;
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer_, &glyph_count);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer_, &glyph_count);

    result.reserve(glyph_count);
    for (unsigned int i = 0; i < glyph_count; i++)
    {
        result.push_back({ info[i].codepoint,
            (int)(pos[i].x_advance >> 6),
            (int)(pos[i].x_offset >> 6),
            (int)(pos[i].y_offset >> 6),
            (int)info[i].cluster });
    }

    return result;
}

uint32_t TextShaper::shape_codepoint(uint32_t codepoint)
{
    if (!font_ || !buffer_)
        return 0;

    hb_buffer_reset(buffer_);
    hb_buffer_add_codepoints(buffer_, &codepoint, 1, 0, 1);
    hb_buffer_guess_segment_properties(buffer_);

    hb_shape(font_, buffer_, nullptr, 0);

    unsigned int glyph_count;
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer_, &glyph_count);
    if (glyph_count > 0)
        return info[0].codepoint;
    return 0;
}

} // namespace spectre
