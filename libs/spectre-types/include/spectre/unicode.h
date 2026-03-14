#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace spectre
{

struct UnicodeRange
{
    uint32_t first;
    uint32_t last;
};

template <size_t N>
constexpr bool in_ranges(uint32_t cp, const std::array<UnicodeRange, N>& ranges)
{
    for (const auto& range : ranges)
    {
        if (cp >= range.first && cp <= range.last)
            return true;
    }
    return false;
}

inline bool utf8_decode_next(std::string_view text, size_t& offset, uint32_t& cp)
{
    if (offset >= text.size())
        return false;

    const auto* bytes = reinterpret_cast<const uint8_t*>(text.data());
    const uint8_t lead = bytes[offset];
    const size_t remaining = text.size() - offset;
    auto is_cont = [](uint8_t byte) { return (byte & 0xC0) == 0x80; };

    if (lead < 0x80)
    {
        cp = lead;
        offset += 1;
        return true;
    }

    if (lead >= 0xC2 && lead <= 0xDF && remaining >= 2 && is_cont(bytes[offset + 1]))
    {
        cp = ((lead & 0x1F) << 6) | (bytes[offset + 1] & 0x3F);
        offset += 2;
        return true;
    }

    if (remaining >= 3)
    {
        const uint8_t b1 = bytes[offset + 1];
        const uint8_t b2 = bytes[offset + 2];
        if (lead == 0xE0 && b1 >= 0xA0 && b1 <= 0xBF && is_cont(b2))
        {
            cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            offset += 3;
            return true;
        }
        if (lead >= 0xE1 && lead <= 0xEC && is_cont(b1) && is_cont(b2))
        {
            cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            offset += 3;
            return true;
        }
        if (lead == 0xED && b1 >= 0x80 && b1 <= 0x9F && is_cont(b2))
        {
            cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            offset += 3;
            return true;
        }
        if (lead >= 0xEE && lead <= 0xEF && is_cont(b1) && is_cont(b2))
        {
            cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            offset += 3;
            return true;
        }
    }

    if (remaining >= 4)
    {
        const uint8_t b1 = bytes[offset + 1];
        const uint8_t b2 = bytes[offset + 2];
        const uint8_t b3 = bytes[offset + 3];
        if (lead == 0xF0 && b1 >= 0x90 && b1 <= 0xBF && is_cont(b2) && is_cont(b3))
        {
            cp = ((lead & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            offset += 4;
            return true;
        }
        if (lead >= 0xF1 && lead <= 0xF3 && is_cont(b1) && is_cont(b2) && is_cont(b3))
        {
            cp = ((lead & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            offset += 4;
            return true;
        }
        if (lead == 0xF4 && b1 >= 0x80 && b1 <= 0x8F && is_cont(b2) && is_cont(b3))
        {
            cp = ((lead & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            offset += 4;
            return true;
        }
    }

    cp = 0xFFFD;
    offset += 1;
    return true;
}

inline uint32_t utf8_first_codepoint(std::string_view text)
{
    if (text.empty())
        return ' ';

    size_t offset = 0;
    uint32_t cp = ' ';
    utf8_decode_next(text, offset, cp);
    return cp;
}

inline bool is_east_asian_wide(uint32_t cp)
{
    return (cp >= 0x1100 && cp <= 0x115F) || cp == 0x2329 || cp == 0x232A || (cp >= 0x2E80 && cp <= 0x303E)
        || (cp >= 0x3040 && cp <= 0xA4CF && cp != 0x303F) || (cp >= 0xAC00 && cp <= 0xD7A3)
        || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19)
        || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF01 && cp <= 0xFF60)
        || (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x20000 && cp <= 0x2FFFD)
        || (cp >= 0x30000 && cp <= 0x3FFFD);
}

inline bool is_default_emoji_presentation(uint32_t cp)
{
    static constexpr std::array kRanges = {
        UnicodeRange{ 0x231A, 0x231B },
        UnicodeRange{ 0x23E9, 0x23EC },
        UnicodeRange{ 0x23F0, 0x23F0 },
        UnicodeRange{ 0x23F3, 0x23F3 },
        UnicodeRange{ 0x25FD, 0x25FE },
        UnicodeRange{ 0x2614, 0x2615 },
        UnicodeRange{ 0x2648, 0x2653 },
        UnicodeRange{ 0x267F, 0x267F },
        UnicodeRange{ 0x2693, 0x2693 },
        UnicodeRange{ 0x26A1, 0x26A1 },
        UnicodeRange{ 0x26AA, 0x26AB },
        UnicodeRange{ 0x26BD, 0x26BE },
        UnicodeRange{ 0x26C4, 0x26C5 },
        UnicodeRange{ 0x26CE, 0x26CE },
        UnicodeRange{ 0x26D4, 0x26D4 },
        UnicodeRange{ 0x26EA, 0x26EA },
        UnicodeRange{ 0x26F2, 0x26F3 },
        UnicodeRange{ 0x26F5, 0x26F5 },
        UnicodeRange{ 0x26FA, 0x26FA },
        UnicodeRange{ 0x26FD, 0x26FD },
        UnicodeRange{ 0x2705, 0x2705 },
        UnicodeRange{ 0x270A, 0x270B },
        UnicodeRange{ 0x2728, 0x2728 },
        UnicodeRange{ 0x274C, 0x274C },
        UnicodeRange{ 0x274E, 0x274E },
        UnicodeRange{ 0x2753, 0x2755 },
        UnicodeRange{ 0x2757, 0x2757 },
        UnicodeRange{ 0x2795, 0x2797 },
        UnicodeRange{ 0x27B0, 0x27B0 },
        UnicodeRange{ 0x27BF, 0x27BF },
        UnicodeRange{ 0x2B1B, 0x2B1C },
        UnicodeRange{ 0x2B50, 0x2B50 },
        UnicodeRange{ 0x2B55, 0x2B55 },
        UnicodeRange{ 0x1F004, 0x1F004 },
        UnicodeRange{ 0x1F0CF, 0x1F0CF },
        UnicodeRange{ 0x1F170, 0x1F171 },
        UnicodeRange{ 0x1F17E, 0x1F17F },
        UnicodeRange{ 0x1F18E, 0x1F18E },
        UnicodeRange{ 0x1F191, 0x1F19A },
        UnicodeRange{ 0x1F1E6, 0x1F1FF },
        UnicodeRange{ 0x1F201, 0x1F202 },
        UnicodeRange{ 0x1F21A, 0x1F21A },
        UnicodeRange{ 0x1F22F, 0x1F22F },
        UnicodeRange{ 0x1F232, 0x1F23A },
        UnicodeRange{ 0x1F250, 0x1F251 },
        UnicodeRange{ 0x1F300, 0x1FAFF },
    };
    return in_ranges(cp, kRanges);
}

inline bool is_emoji_text_presentation_candidate(uint32_t cp)
{
    static constexpr std::array kRanges = {
        UnicodeRange{ 0x00A9, 0x00A9 },
        UnicodeRange{ 0x00AE, 0x00AE },
        UnicodeRange{ 0x203C, 0x203C },
        UnicodeRange{ 0x2049, 0x2049 },
        UnicodeRange{ 0x2122, 0x2122 },
        UnicodeRange{ 0x2139, 0x2139 },
        UnicodeRange{ 0x2194, 0x2199 },
        UnicodeRange{ 0x21A9, 0x21AA },
        UnicodeRange{ 0x231A, 0x231B },
        UnicodeRange{ 0x2328, 0x2328 },
        UnicodeRange{ 0x23CF, 0x23CF },
        UnicodeRange{ 0x23E9, 0x23F3 },
        UnicodeRange{ 0x23F8, 0x23FA },
        UnicodeRange{ 0x24C2, 0x24C2 },
        UnicodeRange{ 0x25AA, 0x25AB },
        UnicodeRange{ 0x25B6, 0x25B6 },
        UnicodeRange{ 0x25C0, 0x25C0 },
        UnicodeRange{ 0x25FB, 0x25FE },
        UnicodeRange{ 0x2600, 0x27BF },
        UnicodeRange{ 0x2934, 0x2935 },
        UnicodeRange{ 0x2B05, 0x2B07 },
        UnicodeRange{ 0x2B1B, 0x2B1C },
        UnicodeRange{ 0x2B50, 0x2B55 },
        UnicodeRange{ 0x3030, 0x3030 },
        UnicodeRange{ 0x303D, 0x303D },
        UnicodeRange{ 0x3297, 0x3297 },
        UnicodeRange{ 0x3299, 0x3299 },
    };
    return in_ranges(cp, kRanges);
}

inline bool is_width_ignorable(uint32_t cp)
{
    static constexpr std::array kRanges = {
        UnicodeRange{ 0x0000, 0x001F },
        UnicodeRange{ 0x007F, 0x009F },
        UnicodeRange{ 0x0300, 0x036F },
        UnicodeRange{ 0x0483, 0x0489 },
        UnicodeRange{ 0x0591, 0x05BD },
        UnicodeRange{ 0x05BF, 0x05BF },
        UnicodeRange{ 0x05C1, 0x05C2 },
        UnicodeRange{ 0x05C4, 0x05C5 },
        UnicodeRange{ 0x05C7, 0x05C7 },
        UnicodeRange{ 0x0610, 0x061A },
        UnicodeRange{ 0x064B, 0x065F },
        UnicodeRange{ 0x0670, 0x0670 },
        UnicodeRange{ 0x06D6, 0x06ED },
        UnicodeRange{ 0x0711, 0x0711 },
        UnicodeRange{ 0x0730, 0x074A },
        UnicodeRange{ 0x07A6, 0x07B0 },
        UnicodeRange{ 0x07EB, 0x07F3 },
        UnicodeRange{ 0x0816, 0x082D },
        UnicodeRange{ 0x0859, 0x085B },
        UnicodeRange{ 0x08D3, 0x0903 },
        UnicodeRange{ 0x093A, 0x094F },
        UnicodeRange{ 0x0951, 0x0957 },
        UnicodeRange{ 0x0962, 0x0963 },
        UnicodeRange{ 0x1AB0, 0x1AFF },
        UnicodeRange{ 0x1DC0, 0x1DFF },
        UnicodeRange{ 0x200C, 0x200F },
        UnicodeRange{ 0x202A, 0x202E },
        UnicodeRange{ 0x2060, 0x206F },
        UnicodeRange{ 0x20D0, 0x20FF },
        UnicodeRange{ 0xFE00, 0xFE0F },
        UnicodeRange{ 0xFE20, 0xFE2F },
        UnicodeRange{ 0xE0100, 0xE01EF },
    };
    return in_ranges(cp, kRanges);
}

inline bool is_regional_indicator(uint32_t cp)
{
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

inline bool is_emoji_modifier(uint32_t cp)
{
    return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

inline bool is_ascii_keycap_base(uint32_t cp)
{
    return cp == '#' || cp == '*' || (cp >= '0' && cp <= '9');
}

inline int cluster_cell_width(std::string_view text)
{
    if (text.empty())
        return 1;

    size_t offset = 0;
    uint32_t base = 0;
    bool have_base = false;
    bool has_vs16 = false;
    bool has_zwj = false;
    bool has_emoji_modifier = false;

    while (offset < text.size())
    {
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;

        if (cp == 0xFE0F)
            has_vs16 = true;
        else if (cp == 0x200D)
            has_zwj = true;
        else if (is_emoji_modifier(cp))
            has_emoji_modifier = true;

        if (!have_base && !is_width_ignorable(cp) && !is_emoji_modifier(cp))
        {
            base = cp;
            have_base = true;
        }
    }

    if (!have_base)
        return 1;

    if (is_east_asian_wide(base) || is_default_emoji_presentation(base) || is_regional_indicator(base))
        return 2;

    if ((has_vs16 || has_zwj || has_emoji_modifier) && is_emoji_text_presentation_candidate(base)
        && !is_ascii_keycap_base(base))
        return 2;

    return 1;
}

} // namespace spectre
