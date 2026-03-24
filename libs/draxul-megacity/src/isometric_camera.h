#pragma once

#include <glm/glm.hpp>

namespace draxul
{

struct GroundFootprint
{
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
};

class IsometricCamera
{
public:
    void set_viewport(int pixel_w, int pixel_h);
    void look_at_world_center(float world_w, float world_h);
    void set_target(const glm::vec3& target);
    void translate_target(float dx, float dz);
    void orbit_target(float radians);
    glm::vec2 pan_delta_for_screen_drag(const glm::vec2& pixel_delta) const;
    glm::vec2 planar_right_vector() const;
    glm::vec2 planar_up_vector() const;

    glm::mat4 view_matrix() const;
    glm::mat4 proj_matrix() const;
    GroundFootprint visible_ground_footprint(float plane_y = 0.0f) const;

private:
    glm::vec3 position_{ -6.0f, 7.0f, -6.0f };
    glm::vec3 target_{ 2.5f, 0.0f, 2.5f };
    glm::vec3 follow_offset_{ -5.0f, 6.25f, -5.0f };
    float ortho_half_height_ = 4.0f;
    float aspect_ = 1.0f;
    int viewport_pixel_h_ = 1;
};

} // namespace draxul
