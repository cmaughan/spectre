#pragma once

#include <filesystem>

namespace draxul
{

std::filesystem::path executable_directory();
std::filesystem::path bundled_asset_path(const std::filesystem::path& relative_path);

} // namespace draxul
