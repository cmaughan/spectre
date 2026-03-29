#include "live_city_metrics.h"

#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace draxul
{

namespace
{

struct PerfFunctionLookup
{
    float frame_fraction = 0.0f;
    float smoothed_frame_fraction = 0.0f;
    float heat = 0.0f;
};

std::string perf_owner_function_key(std::string_view owner_qualified_name, std::string_view function_name)
{
    std::string key;
    key.reserve(owner_qualified_name.size() + function_name.size() + 1);
    key.append(owner_qualified_name);
    key.push_back('\n');
    key.append(function_name);
    return key;
}

std::string perf_function_key(
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name)
{
    std::string key;
    key.reserve(source_file_path.size() + owner_qualified_name.size() + function_name.size() + 2);
    key.append(source_file_path);
    key.push_back('\n');
    key.append(owner_qualified_name);
    key.push_back('\n');
    key.append(function_name);
    return key;
}

std::string short_qualified_name(std::string_view qualified_name)
{
    const size_t last_scope = qualified_name.rfind("::");
    if (last_scope == std::string_view::npos)
        return std::string(qualified_name);
    return std::string(qualified_name.substr(last_scope + 2));
}

void accumulate_perf_function(
    std::unordered_map<std::string, PerfFunctionLookup>& lookup,
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name,
    const RuntimePerfFunctionTiming& timing)
{
    if (source_file_path.empty() || owner_qualified_name.empty() || function_name.empty())
        return;

    PerfFunctionLookup& entry
        = lookup[perf_function_key(source_file_path, owner_qualified_name, function_name)];
    entry.frame_fraction = std::max(entry.frame_fraction, timing.frame_fraction);
    entry.smoothed_frame_fraction = std::max(entry.smoothed_frame_fraction, timing.smoothed_frame_fraction);
    entry.heat = std::max(entry.heat, timing.normalized_heat);
}

void accumulate_perf_function_without_file(
    std::unordered_map<std::string, PerfFunctionLookup>& lookup,
    std::string_view owner_qualified_name,
    std::string_view function_name,
    const RuntimePerfFunctionTiming& timing)
{
    if (owner_qualified_name.empty() || function_name.empty())
        return;

    PerfFunctionLookup& entry = lookup[perf_owner_function_key(owner_qualified_name, function_name)];
    entry.frame_fraction = std::max(entry.frame_fraction, timing.frame_fraction);
    entry.smoothed_frame_fraction = std::max(entry.smoothed_frame_fraction, timing.smoothed_frame_fraction);
    entry.heat = std::max(entry.heat, timing.normalized_heat);
}

struct PerfLookupSet
{
    std::unordered_map<std::string, PerfFunctionLookup> by_file;
    std::unordered_map<std::string, PerfFunctionLookup> by_owner_function;
};

PerfLookupSet build_perf_lookup(const RuntimePerfSnapshot* perf_snapshot)
{
    PerfLookupSet lookup;
    if (!perf_snapshot)
        return lookup;

    lookup.by_file.reserve(perf_snapshot->functions.size() * 2u);
    lookup.by_owner_function.reserve(perf_snapshot->functions.size() * 2u);
    for (const RuntimePerfFunctionTiming& timing : perf_snapshot->functions)
    {
        if (timing.source_file_path.empty() || timing.function_name.empty())
            continue;

        accumulate_perf_function(
            lookup.by_file,
            timing.source_file_path,
            timing.owner_qualified_name,
            timing.function_name,
            timing);
        accumulate_perf_function_without_file(
            lookup.by_owner_function,
            timing.owner_qualified_name,
            timing.function_name,
            timing);

        const std::string owner_short_name = short_qualified_name(timing.owner_qualified_name);
        if (owner_short_name != timing.owner_qualified_name)
        {
            accumulate_perf_function(
                lookup.by_file,
                timing.source_file_path,
                owner_short_name,
                timing.function_name,
                timing);
            accumulate_perf_function_without_file(
                lookup.by_owner_function,
                owner_short_name,
                timing.function_name,
                timing);
        }
    }

    return lookup;
}

PerfFunctionLookup find_perf_function(
    const PerfLookupSet& lookup,
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name)
{
    const auto exact_it = lookup.by_file.find(perf_function_key(source_file_path, owner_qualified_name, function_name));
    if (exact_it != lookup.by_file.end())
        return exact_it->second;

    const auto owner_it = lookup.by_owner_function.find(perf_owner_function_key(owner_qualified_name, function_name));
    if (owner_it != lookup.by_owner_function.end())
        return owner_it->second;

    return {};
}

} // namespace

LiveCityMetricsSnapshot build_live_city_metrics_snapshot(
    const SemanticMegacityModel& model,
    const RuntimePerfSnapshot* perf_snapshot)
{
    LiveCityMetricsSnapshot snapshot;
    snapshot.generation = perf_snapshot ? perf_snapshot->generation : 0;
    const auto perf_lookup = build_perf_lookup(perf_snapshot);

    for (const auto& module : model.modules)
    {
        snapshot.buildings.reserve(snapshot.buildings.size() + module.buildings.size());
        for (const auto& building : module.buildings)
        {
            float building_frame_fraction = 0.0f;
            float building_smoothed_fraction = 0.0f;
            float building_heat = 0.0f;

            if (!building.layers.empty())
            {
                for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
                {
                    const SemanticBuildingLayer& layer = building.layers[layer_index];
                    const PerfFunctionLookup perf = find_perf_function(
                        perf_lookup,
                        building.source_file_path,
                        building.qualified_name,
                        layer.function_name);
                    snapshot.functions.push_back({
                        .source_file_path = building.source_file_path,
                        .module_path = building.module_path,
                        .qualified_name = building.qualified_name,
                        .function_name = layer.function_name,
                        .layer_index = static_cast<uint32_t>(layer_index),
                        .layer_count = static_cast<uint32_t>(building.layers.size()),
                        .frame_fraction = perf.frame_fraction,
                        .smoothed_frame_fraction = perf.smoothed_frame_fraction,
                        .heat = perf.heat,
                    });
                    building_frame_fraction = std::max(building_frame_fraction, perf.frame_fraction);
                    building_smoothed_fraction = std::max(building_smoothed_fraction, perf.smoothed_frame_fraction);
                    building_heat = std::max(building_heat, perf.heat);
                }
            }

            snapshot.buildings.push_back({
                .source_file_path = building.source_file_path,
                .module_path = building.module_path,
                .qualified_name = building.qualified_name,
                .display_name = building.display_name.empty() ? building.qualified_name : building.display_name,
                .is_struct = building.is_struct,
                .frame_fraction = building_frame_fraction,
                .smoothed_frame_fraction = building_smoothed_fraction,
                .heat = building_heat,
            });
        }
    }

    return snapshot;
}

} // namespace draxul
