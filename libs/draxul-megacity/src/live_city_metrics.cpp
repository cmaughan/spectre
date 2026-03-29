#include "live_city_metrics.h"

#include <algorithm>
#include <cstdint>

namespace draxul
{

namespace
{

uint64_t hash_append(uint64_t hash, std::string_view value)
{
    constexpr uint64_t kFnvOffset = 1469598103934665603ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    if (hash == 0)
        hash = kFnvOffset;
    for (const unsigned char ch : value)
    {
        hash ^= static_cast<uint64_t>(ch);
        hash *= kFnvPrime;
    }
    hash ^= 0xffull;
    hash *= kFnvPrime;
    return hash;
}

uint64_t hash_append_uint64(uint64_t hash, uint64_t value)
{
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    for (int shift = 0; shift < 64; shift += 8)
    {
        hash ^= static_cast<uint64_t>((value >> shift) & 0xffull);
        hash *= kFnvPrime;
    }
    return hash;
}

float hash_to_unit_float(uint64_t hash)
{
    constexpr float kInv24Bit = 1.0f / 16777215.0f;
    return static_cast<float>(hash & 0x00ffffffu) * kInv24Bit;
}

float stable_heat(
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name,
    std::string_view function_name = {},
    uint32_t layer_index = 0)
{
    uint64_t hash = 0;
    hash = hash_append(hash, source_file_path);
    hash = hash_append(hash, module_path);
    hash = hash_append(hash, qualified_name);
    hash = hash_append(hash, function_name);
    hash = hash_append_uint64(hash, layer_index);
    return hash_to_unit_float(hash);
}

} // namespace

LiveCityMetricsSnapshot build_live_city_metrics_snapshot(const SemanticMegacityModel& model)
{
    LiveCityMetricsSnapshot snapshot;
    snapshot.generation = 1;

    for (const auto& module : model.modules)
    {
        snapshot.buildings.reserve(snapshot.buildings.size() + module.buildings.size());
        for (const auto& building : module.buildings)
        {
            float building_heat = stable_heat(
                building.source_file_path,
                building.module_path,
                building.qualified_name);
            if (!building.layers.empty())
            {
                float total_heat = 0.0f;
                for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
                {
                    const SemanticBuildingLayer& layer = building.layers[layer_index];
                    const float heat = stable_heat(
                        building.source_file_path,
                        building.module_path,
                        building.qualified_name,
                        layer.function_name,
                        static_cast<uint32_t>(layer_index));
                    snapshot.functions.push_back({
                        .source_file_path = building.source_file_path,
                        .module_path = building.module_path,
                        .qualified_name = building.qualified_name,
                        .function_name = layer.function_name,
                        .layer_index = static_cast<uint32_t>(layer_index),
                        .layer_count = static_cast<uint32_t>(building.layers.size()),
                        .heat = heat,
                    });
                    total_heat += heat;
                }
                building_heat = std::clamp(
                    total_heat / static_cast<float>(building.layers.size()),
                    0.0f,
                    1.0f);
            }

            snapshot.buildings.push_back({
                .source_file_path = building.source_file_path,
                .module_path = building.module_path,
                .qualified_name = building.qualified_name,
                .display_name = building.display_name.empty() ? building.qualified_name : building.display_name,
                .is_struct = building.is_struct,
                .heat = building_heat,
            });
        }
    }

    return snapshot;
}

} // namespace draxul
