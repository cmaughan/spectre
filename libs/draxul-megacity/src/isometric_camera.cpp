#include "isometric_camera.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace draxul
{

namespace
{

glm::vec2 normalized_planar(const glm::vec3& v, const glm::vec2& fallback)
{
    const glm::vec2 planar{ v.x, v.z };
    const float len = glm::length(planar);
    if (len <= 1e-5f)
        return fallback;
    return planar / len;
}

} // namespace

void IsometricCamera::set_viewport(int pixel_w, int pixel_h)
{
    viewport_pixel_h_ = std::max(pixel_h, 1);
    aspect_ = (pixel_h > 0) ? static_cast<float>(pixel_w) / static_cast<float>(pixel_h) : 1.0f;
}

void IsometricCamera::look_at_world_center(float world_w, float world_h)
{
    const float max_dim = std::max(world_w, world_h);
    ortho_half_height_ = std::max(4.0f, max_dim * 0.8f);
    follow_offset_ = glm::vec3(-max_dim, max_dim * 1.25f, -max_dim);
    set_target({ world_w * 0.5f, 0.0f, world_h * 0.5f });
}

void IsometricCamera::set_target(const glm::vec3& target)
{
    target_ = target;
    position_ = target_ + follow_offset_;
}

void IsometricCamera::translate_target(float dx, float dz)
{
    set_target(target_ + glm::vec3(dx, 0.0f, dz));
}

void IsometricCamera::orbit_target(float radians)
{
    const float radius = glm::length(glm::vec2(follow_offset_.x, follow_offset_.z));
    if (radius <= 0.0f)
        return;

    const float angle = std::atan2(follow_offset_.z, follow_offset_.x) + radians;
    follow_offset_.x = std::cos(angle) * radius;
    follow_offset_.z = std::sin(angle) * radius;
    position_ = target_ + follow_offset_;
}

glm::vec2 IsometricCamera::pan_delta_for_screen_drag(const glm::vec2& pixel_delta) const
{
    const glm::vec3 forward = glm::normalize(target_ - position_);
    const glm::vec3 right_3d = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up_3d = glm::normalize(glm::cross(right_3d, forward));
    const glm::vec2 right = normalized_planar(right_3d, glm::vec2(1.0f, 0.0f));
    const glm::vec2 up = normalized_planar(up_3d, glm::vec2(0.0f, 1.0f));

    const float world_units_per_pixel = (2.0f * ortho_half_height_) / static_cast<float>(viewport_pixel_h_);
    const float up_planar_length = std::max(glm::length(glm::vec2(up_3d.x, up_3d.z)), 1e-5f);

    return (-pixel_delta.x * world_units_per_pixel) * right
        + ((pixel_delta.y * world_units_per_pixel) / up_planar_length) * up;
}

glm::vec2 IsometricCamera::planar_right_vector() const
{
    const glm::vec3 forward = glm::normalize(target_ - position_);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    return normalized_planar(right, glm::vec2(1.0f, 0.0f));
}

glm::vec2 IsometricCamera::planar_up_vector() const
{
    const glm::vec3 forward = glm::normalize(target_ - position_);
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    return normalized_planar(up, glm::vec2(0.0f, 1.0f));
}

glm::mat4 IsometricCamera::view_matrix() const
{
    return glm::lookAtRH(position_, target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 IsometricCamera::proj_matrix() const
{
    const float half_width = ortho_half_height_ * aspect_;
    return glm::orthoRH_ZO(-half_width, half_width, -ortho_half_height_, ortho_half_height_, 0.1f, 100.0f);
}

GroundFootprint IsometricCamera::visible_ground_footprint(float plane_y) const
{
    const glm::mat4 inv_view_proj = glm::inverse(proj_matrix() * view_matrix());

    GroundFootprint footprint;
    footprint.min_x = std::numeric_limits<float>::max();
    footprint.max_x = std::numeric_limits<float>::lowest();
    footprint.min_z = std::numeric_limits<float>::max();
    footprint.max_z = std::numeric_limits<float>::lowest();

    const glm::vec2 corners[4] = {
        { -1.0f, -1.0f },
        { 1.0f, -1.0f },
        { 1.0f, 1.0f },
        { -1.0f, 1.0f },
    };

    for (const glm::vec2& corner : corners)
    {
        glm::vec4 near_world = inv_view_proj * glm::vec4(corner, 0.0f, 1.0f);
        glm::vec4 far_world = inv_view_proj * glm::vec4(corner, 1.0f, 1.0f);
        near_world /= near_world.w;
        far_world /= far_world.w;

        const glm::vec3 near_pos = glm::vec3(near_world);
        const glm::vec3 far_pos = glm::vec3(far_world);
        const glm::vec3 ray = far_pos - near_pos;
        if (std::abs(ray.y) < 1e-5f)
            continue;

        const float t = (plane_y - near_pos.y) / ray.y;
        const glm::vec3 point = near_pos + ray * t;
        footprint.min_x = std::min(footprint.min_x, point.x);
        footprint.max_x = std::max(footprint.max_x, point.x);
        footprint.min_z = std::min(footprint.min_z, point.z);
        footprint.max_z = std::max(footprint.max_z, point.z);
    }

    if (footprint.min_x > footprint.max_x || footprint.min_z > footprint.max_z)
    {
        footprint.min_x = target_.x - ortho_half_height_;
        footprint.max_x = target_.x + ortho_half_height_;
        footprint.min_z = target_.z - ortho_half_height_;
        footprint.max_z = target_.z + ortho_half_height_;
    }

    return footprint;
}

} // namespace draxul
