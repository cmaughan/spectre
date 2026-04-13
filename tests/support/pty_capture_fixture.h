#pragma once

#include <draxul/base64.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace draxul::tests
{

struct PtyCaptureChunk
{
    std::string host_name;
    std::string bytes;
};

struct PtyCaptureFile
{
    std::vector<PtyCaptureChunk> chunks;
};

inline std::optional<PtyCaptureFile> load_pty_capture_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;

    std::string line;
    if (!std::getline(in, line) || line != "draxul-pty-capture-v1")
        return std::nullopt;

    PtyCaptureFile capture;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        if (!line.starts_with("chunk "))
            return std::nullopt;

        const size_t first_space = line.find(' ', 6);
        if (first_space == std::string::npos)
            return std::nullopt;

        const std::string_view encoded_host(line.data() + 6, first_space - 6);
        const std::string_view encoded_bytes(line.data() + first_space + 1,
            line.size() - first_space - 1);

        auto decoded_host = base64_decode(encoded_host);
        auto decoded_bytes = base64_decode(encoded_bytes);
        if (!decoded_host || !decoded_bytes)
            return std::nullopt;

        capture.chunks.push_back(PtyCaptureChunk{
            .host_name = std::move(*decoded_host),
            .bytes = std::move(*decoded_bytes),
        });
    }

    return capture;
}

} // namespace draxul::tests
