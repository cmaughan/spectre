#include "font_engine.h"
#include <hb-ft.h>
#include <utility>

namespace draxul
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

void TextShaper::initialize(hb_font_t* font, bool enable_ligatures)
{
    shutdown();
    font_ = font;
    buffer_ = hb_buffer_create();
    enable_ligatures_ = enable_ligatures;
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

    constexpr hb_feature_t disabled_ligature_features[] = {
        { HB_TAG('l', 'i', 'g', 'a'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END },
        { HB_TAG('c', 'l', 'i', 'g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END },
        { HB_TAG('c', 'a', 'l', 't'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END },
        { HB_TAG('d', 'l', 'i', 'g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END },
    };
    hb_shape(font_, buffer_,
        enable_ligatures_ ? nullptr : disabled_ligature_features,
        enable_ligatures_ ? 0u : static_cast<unsigned int>(std::size(disabled_ligature_features)));

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

} // namespace draxul
