#pragma once

#include "font_engine.h"
#include "font_resolver.h"

#include <draxul/unicode.h>

#include <algorithm>
#include <string>
#include <unordered_map>

namespace draxul
{

namespace
{

inline bool can_render_cluster(FT_Face face, TextShaper& shaper, const std::string& text)
{
    if (!face)
        return false;

    auto shaped = shaper.shape(text);
    if (shaped.empty())
        return false;

    bool has_glyph = false;
    for (const auto& glyph : shaped)
    {
        if (glyph.glyph_id == 0)
            return false;
        if (FT_Load_Glyph(face, glyph.glyph_id, FT_LOAD_DEFAULT))
            return false;
        has_glyph = true;
    }

    return has_glyph;
}

inline bool cluster_prefers_color_font(std::string_view text)
{
    size_t offset = 0;
    uint32_t base = 0;
    bool have_base = false;
    bool has_vs16 = false;
    bool has_zwj = false;
    bool has_emoji_modifier = false;
    bool has_keycap = false;

    while (offset < text.size())
    {
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;

        if (cp == 0xFE0F)
            has_vs16 = true;
        else if (cp == 0x200D)
            has_zwj = true;
        else if (cp == 0x20E3)
            has_keycap = true;
        else if (is_emoji_modifier(cp))
            has_emoji_modifier = true;

        if (!have_base && !is_width_ignorable(cp) && !is_emoji_modifier(cp))
        {
            base = cp;
            have_base = true;
        }
    }

    if (!have_base)
        return false;

    if (is_default_emoji_presentation(base) || is_regional_indicator(base))
        return true;

    if ((has_vs16 || has_zwj || has_emoji_modifier) && is_emoji_text_presentation_candidate(base))
        return true;

    if (has_keycap && is_ascii_keycap_base(base))
        return true;

    return false;
}

inline bool font_has_color(FT_Face face)
{
    return face && FT_HAS_COLOR(face);
}

} // namespace

// Selects the best font face for a given text cluster, with per-cluster caching.
class FontSelector
{
public:
    struct Selection
    {
        FT_Face face = nullptr;
        TextShaper* shaper = nullptr;
    };

    void reset_cache()
    {
        cache_.clear();
        bold_cache_.clear();
        italic_cache_.clear();
        bold_italic_cache_.clear();
    }

    void set_cache_limit(size_t limit)
    {
        cache_limit_ = std::max<size_t>(1, limit);
    }

    // Returns the face+shaper best suited to render `text`.
    Selection select(const std::string& text, FontResolver& resolver, bool is_bold = false, bool is_italic = false)
    {
        if (is_bold && is_italic && resolver.has_bold_italic())
        {
            auto bi_it = bold_italic_cache_.find(text);
            if (bi_it != bold_italic_cache_.end())
            {
                if (bi_it->second < 0)
                    return { resolver.bold_italic().face(), &resolver.bold_italic_shaper() };
                auto& fallbacks = resolver.fallbacks();
                int idx = bi_it->second;
                if (idx < (int)fallbacks.size())
                    return { fallbacks[(size_t)idx].font.face(), &fallbacks[(size_t)idx].shaper };
            }

            if (can_render_cluster(resolver.bold_italic().face(), resolver.bold_italic_shaper(), text))
            {
                store_bold_italic(text, -1);
                return { resolver.bold_italic().face(), &resolver.bold_italic_shaper() };
            }
            // Bold-italic face can't render this cluster — fall through
        }

        if (is_bold && resolver.has_bold())
        {
            auto bold_it = bold_cache_.find(text);
            if (bold_it != bold_cache_.end())
            {
                if (bold_it->second < 0)
                    return { resolver.bold().face(), &resolver.bold_shaper() };
                // Fall through to regular path for fallback index
                auto& fallbacks = resolver.fallbacks();
                int idx = bold_it->second;
                if (idx < (int)fallbacks.size())
                    return { fallbacks[(size_t)idx].font.face(), &fallbacks[(size_t)idx].shaper };
            }

            if (can_render_cluster(resolver.bold().face(), resolver.bold_shaper(), text))
            {
                store_bold(text, -1);
                return { resolver.bold().face(), &resolver.bold_shaper() };
            }
            // Bold face can't render this cluster — fall through to regular path
        }

        if (is_italic && resolver.has_italic())
        {
            auto italic_it = italic_cache_.find(text);
            if (italic_it != italic_cache_.end())
            {
                if (italic_it->second < 0)
                    return { resolver.italic().face(), &resolver.italic_shaper() };
                auto& fallbacks = resolver.fallbacks();
                int idx = italic_it->second;
                if (idx < (int)fallbacks.size())
                    return { fallbacks[(size_t)idx].font.face(), &fallbacks[(size_t)idx].shaper };
            }

            if (can_render_cluster(resolver.italic().face(), resolver.italic_shaper(), text))
            {
                store_italic(text, -1);
                return { resolver.italic().face(), &resolver.italic_shaper() };
            }
            // Italic face can't render this cluster — fall through to regular path
        }

        auto it = cache_.find(text);
        if (it != cache_.end())
        {
            int idx = it->second;
            if (idx < 0)
                return { resolver.primary().face(), &resolver.primary_shaper() };
            auto& fallbacks = resolver.fallbacks();
            if (idx < (int)fallbacks.size())
                return { fallbacks[(size_t)idx].font.face(), &fallbacks[(size_t)idx].shaper };
        }

        auto& fallbacks = resolver.fallbacks();

        if (cluster_prefers_color_font(text))
        {
            if (font_has_color(resolver.primary().face())
                && can_render_cluster(resolver.primary().face(), resolver.primary_shaper(), text))
            {
                store(text, -1);
                return { resolver.primary().face(), &resolver.primary_shaper() };
            }

            for (int i = 0; i < (int)fallbacks.size(); i++)
            {
                if (!resolver.ensure_loaded((size_t)i))
                    continue;
                auto& fb = fallbacks[(size_t)i];
                if (font_has_color(fb.font.face()) && can_render_cluster(fb.font.face(), fb.shaper, text))
                {
                    store(text, i);
                    return { fb.font.face(), &fb.shaper };
                }
            }
        }

        if (can_render_cluster(resolver.primary().face(), resolver.primary_shaper(), text))
        {
            store(text, -1);
            return { resolver.primary().face(), &resolver.primary_shaper() };
        }

        for (int i = 0; i < (int)fallbacks.size(); i++)
        {
            if (!resolver.ensure_loaded((size_t)i))
                continue;
            auto& fb = fallbacks[(size_t)i];
            if (can_render_cluster(fb.font.face(), fb.shaper, text))
            {
                store(text, i);
                return { fb.font.face(), &fb.shaper };
            }
        }

        store(text, -1);
        return { resolver.primary().face(), &resolver.primary_shaper() };
    }

    size_t cache_size() const
    {
        return cache_.size();
    }

private:
    void store(const std::string& text, int idx)
    {
        if (cache_.size() >= cache_limit_)
            cache_.clear();
        cache_[text] = idx;
    }

    void store_bold(const std::string& text, int idx)
    {
        if (bold_cache_.size() >= cache_limit_)
            bold_cache_.clear();
        bold_cache_[text] = idx;
    }

    void store_italic(const std::string& text, int idx)
    {
        if (italic_cache_.size() >= cache_limit_)
            italic_cache_.clear();
        italic_cache_[text] = idx;
    }

    void store_bold_italic(const std::string& text, int idx)
    {
        if (bold_italic_cache_.size() >= cache_limit_)
            bold_italic_cache_.clear();
        bold_italic_cache_[text] = idx;
    }

    std::unordered_map<std::string, int> cache_;
    std::unordered_map<std::string, int> bold_cache_;
    std::unordered_map<std::string, int> italic_cache_;
    std::unordered_map<std::string, int> bold_italic_cache_;
    size_t cache_limit_ = TextServiceConfig::DEFAULT_FONT_CHOICE_CACHE_LIMIT;
};

} // namespace draxul
