#pragma once

#include "semantic_city_layout.h"

#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{

struct LiveCityBuildingMetric
{
    std::string source_file_path;
    std::string module_path;
    std::string qualified_name;
    std::string display_name;
    bool is_struct = false;
    float heat = 0.0f;
};

struct LiveCityFunctionMetric
{
    std::string source_file_path;
    std::string module_path;
    std::string qualified_name;
    std::string function_name;
    uint32_t layer_index = 0;
    uint32_t layer_count = 0;
    float heat = 0.0f;
};

struct LiveCityMetricsSnapshot
{
    uint64_t generation = 0;
    std::vector<LiveCityBuildingMetric> buildings;
    std::vector<LiveCityFunctionMetric> functions;
};

[[nodiscard]] LiveCityMetricsSnapshot build_live_city_metrics_snapshot(
    const SemanticMegacityModel& model);

} // namespace draxul
