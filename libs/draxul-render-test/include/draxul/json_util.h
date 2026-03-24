#pragma once

#include <string>
#include <string_view>

namespace draxul
{

inline std::string json_escape_string(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char ch : s)
    {
        switch (ch)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

} // namespace draxul
