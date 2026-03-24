#include <catch2/catch_all.hpp>

#ifdef DRAXUL_ENABLE_MEGACITY

#include "isometric_camera.h"
#include "isometric_world.h"
#include "mesh_library.h"
#include <numbers>

using namespace draxul;

namespace
{

float triangle_up_normal_y(const MeshData& mesh, size_t triangle_index)
{
    const size_t base = triangle_index * 3;
    const glm::vec3 p0 = mesh.vertices[mesh.indices[base + 0]].position;
    const glm::vec3 p1 = mesh.vertices[mesh.indices[base + 1]].position;
    const glm::vec3 p2 = mesh.vertices[mesh.indices[base + 2]].position;
    return glm::cross(p1 - p0, p2 - p0).y;
}

} // namespace

TEST_CASE("megacity world maps grid coordinates to tile centers", "[megacity]")
{
    IsometricWorld world;

    const glm::vec3 origin = world.grid_to_world(0, 0);
    const glm::vec3 corner = world.grid_to_world(4, 4);
    const glm::vec3 elevated = world.grid_to_world(2, 3, 2.0f);
    const glm::vec3 fractional = world.grid_to_world(2.25f, 3.5f);

    CHECK(origin.x == Catch::Approx(0.5f));
    CHECK(origin.y == Catch::Approx(0.0f));
    CHECK(origin.z == Catch::Approx(0.5f));

    CHECK(corner.x == Catch::Approx(4.5f));
    CHECK(corner.z == Catch::Approx(4.5f));

    CHECK(elevated.x == Catch::Approx(2.5f));
    CHECK(elevated.y == Catch::Approx(2.0f));
    CHECK(elevated.z == Catch::Approx(3.5f));

    CHECK(fractional.x == Catch::Approx(2.75f));
    CHECK(fractional.z == Catch::Approx(4.0f));
}

TEST_CASE("megacity camera projection responds to viewport aspect", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);

    camera.set_viewport(100, 100);
    const glm::mat4 square = camera.proj_matrix();

    camera.set_viewport(200, 100);
    const glm::mat4 wide = camera.proj_matrix();

    CHECK(wide[0][0] < square[0][0]);
    CHECK(wide[1][1] == Catch::Approx(square[1][1]));
}

TEST_CASE("megacity camera footprint covers the centered world", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(160, 100);

    const GroundFootprint footprint = camera.visible_ground_footprint();

    CHECK(footprint.min_x < 0.5f);
    CHECK(footprint.max_x > 4.5f);
    CHECK(footprint.min_z < 0.5f);
    CHECK(footprint.max_z > 4.5f);
}

TEST_CASE("megacity camera footprint follows a retargeted focus point", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(160, 100);

    const GroundFootprint centered = camera.visible_ground_footprint();
    camera.set_target({ 14.5f, 0.0f, 9.5f });
    const GroundFootprint shifted = camera.visible_ground_footprint();

    CHECK(shifted.min_x > centered.min_x + 8.0f);
    CHECK(shifted.max_x > centered.max_x + 8.0f);
    CHECK(shifted.min_z > centered.min_z + 4.0f);
    CHECK(shifted.max_z > centered.max_z + 4.0f);
}

TEST_CASE("megacity camera orbit keeps looking at the same world focus", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);
    camera.set_viewport(160, 100);
    camera.set_target({ 8.5f, 0.0f, 6.5f });

    const glm::mat4 before_view = camera.view_matrix();
    const GroundFootprint before = camera.visible_ground_footprint();
    camera.orbit_target(std::numbers::pi_v<float> * 0.5f);
    const glm::mat4 after_view = camera.view_matrix();
    const GroundFootprint after = camera.visible_ground_footprint();

    CHECK(after.min_x < 8.5f);
    CHECK(after.max_x > 8.5f);
    CHECK(after.min_z < 6.5f);
    CHECK(after.max_z > 6.5f);
    CHECK(after_view[2][0] != Catch::Approx(before_view[2][0]));
    CHECK(after_view[0][2] != Catch::Approx(before_view[0][2]));
    CHECK(before.min_x < 8.5f);
    CHECK(before.max_x > 8.5f);
}

TEST_CASE("megacity camera planar axes follow the current view", "[megacity]")
{
    IsometricCamera camera;
    camera.look_at_world_center(5.0f, 5.0f);

    const glm::vec2 initial_right = camera.planar_right_vector();
    const glm::vec2 initial_up = camera.planar_up_vector();
    camera.orbit_target(std::numbers::pi_v<float> * 0.5f);
    const glm::vec2 rotated_right = camera.planar_right_vector();
    const glm::vec2 rotated_up = camera.planar_up_vector();

    CHECK(glm::length(initial_right) == Catch::Approx(1.0f));
    CHECK(glm::length(initial_up) == Catch::Approx(1.0f));
    CHECK(std::abs(glm::dot(initial_right, initial_up)) < 0.01f);
    CHECK(glm::length(rotated_right) == Catch::Approx(1.0f));
    CHECK(glm::length(rotated_up) == Catch::Approx(1.0f));
    CHECK(std::abs(glm::dot(rotated_right, rotated_up)) < 0.01f);
    CHECK(std::abs(glm::dot(initial_right, rotated_right)) < 0.01f);
    CHECK(std::abs(glm::dot(initial_up, rotated_up)) < 0.01f);
}

TEST_CASE("megacity mesh library builds expected primitive counts", "[megacity]")
{
    const MeshData cube = build_unit_cube_mesh();
    const MeshData filled = build_grid_mesh(2, 2, 1.0f);

    FloorGridSpec grid;
    grid.enabled = true;
    grid.min_x = 0;
    grid.max_x = 2;
    grid.min_z = 0;
    grid.max_z = 2;
    grid.tile_size = 1.0f;
    grid.line_width = 0.04f;

    const MeshData outline = build_outline_grid_mesh(grid);

    CHECK(cube.vertices.size() == 24);
    CHECK(cube.indices.size() == 36);

    CHECK(filled.vertices.size() == 16);
    CHECK(filled.indices.size() == 24);
    CHECK(triangle_up_normal_y(filled, 0) > 0.0f);

    CHECK(outline.vertices.size() == 24);
    CHECK(outline.indices.size() == 36);
    CHECK(triangle_up_normal_y(outline, 0) > 0.0f);
}

#endif
