#pragma once

#include "isometric_scene_types.h"
#include <memory>

namespace draxul
{

class IsometricCamera;
class SceneWorld;
struct MegaCityCodeConfig;
struct SignLabelAtlas;

struct SceneSnapshotResult
{
    SceneSnapshot snapshot;
    float world_span = 5.0f;
};

// Build a render-ready SceneSnapshot by querying the ECS world and camera state.
SceneSnapshotResult build_scene_snapshot(
    const IsometricCamera& camera,
    const SceneWorld& world,
    const MegaCityCodeConfig& config,
    const std::shared_ptr<SignLabelAtlas>& label_atlas);

} // namespace draxul
