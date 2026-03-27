#pragma once

#include <draxul/megacity_code_config.h>

namespace draxul
{

inline float building_base_elevation(const MegaCityCodeConfig& config)
{
    return config.sidewalk_surface_lift + config.sidewalk_surface_height;
}

inline float world_floor_height(const MegaCityCodeConfig& config)
{
    return config.road_surface_height * config.world_floor_height_scale;
}

} // namespace draxul
