#include <draxul/tree_generator.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace draxul
{

namespace
{

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;

struct BranchFrame
{
    glm::vec3 center{ 0.0f };
    glm::vec3 axis{ 0.0f, 1.0f, 0.0f };
    glm::vec3 basis_u{ 1.0f, 0.0f, 0.0f };
    glm::vec3 basis_v{ 0.0f, 0.0f, 1.0f };
    float radius = 0.1f;
    float t = 0.0f;
    float distance = 0.0f;
};

float hash_to_unit_float(uint64_t seed, uint32_t a, uint32_t b)
{
    uint64_t x = seed;
    x ^= static_cast<uint64_t>(a) + 0x9e3779b97f4a7c15ULL + (x << 6U) + (x >> 2U);
    x ^= static_cast<uint64_t>(b) + 0x9e3779b97f4a7c15ULL + (x << 6U) + (x >> 2U);
    x ^= x >> 30U;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27U;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31U;
    constexpr double kInvMax = 1.0 / static_cast<double>(std::numeric_limits<uint64_t>::max());
    return static_cast<float>(static_cast<double>(x) * kInvMax);
}

int hash_to_int(uint64_t seed, uint32_t a, uint32_t b, int min_value, int max_value)
{
    if (max_value <= min_value)
        return min_value;
    const float unit = hash_to_unit_float(seed, a, b);
    return min_value + static_cast<int>(std::floor(unit * static_cast<float>((max_value - min_value) + 1)));
}

glm::vec3 orthogonal_unit(const glm::vec3& axis)
{
    const glm::vec3 up = std::abs(axis.y) < 0.95f
        ? glm::vec3(0.0f, 1.0f, 0.0f)
        : glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::normalize(glm::cross(up, axis));
}

glm::vec3 project_basis(const glm::vec3& previous_basis, const glm::vec3& axis)
{
    glm::vec3 projected = previous_basis - axis * glm::dot(previous_basis, axis);
    if (glm::dot(projected, projected) < 1e-6f)
        projected = orthogonal_unit(axis);
    return glm::normalize(projected);
}

glm::vec3 bark_color_for_vertex(const DraxulTreeParams& params, float t, uint32_t ring_index, uint32_t segment_index)
{
    const glm::vec3 base = glm::mix(params.bark_color_root, params.bark_color_tip, std::clamp(t, 0.0f, 1.0f));
    const float noise = (hash_to_unit_float(params.seed, ring_index, segment_index) * 2.0f - 1.0f) * params.bark_color_noise;
    return glm::clamp(base + glm::vec3(noise), glm::vec3(0.0f), glm::vec3(1.0f));
}

std::vector<float> build_ring_distances(const DraxulTreeParams& params, float length, float spacing_scale)
{
    std::vector<float> distances;
    distances.reserve(static_cast<size_t>(std::ceil(length / std::max(params.base_ring_spacing, 0.05f))) + 2U);
    distances.push_back(0.0f);

    float distance = 0.0f;
    while (distance < length)
    {
        const float t = length > 1e-5f ? std::clamp(distance / length, 0.0f, 1.0f) : 1.0f;
        const float spacing = params.base_ring_spacing
            * std::lerp(1.0f, params.tip_ring_spacing_scale, t)
            * std::max(spacing_scale, 0.2f)
            * params.overall_scale;
        distance = std::min(distance + std::max(spacing, 0.025f), length);
        if (distance > distances.back())
            distances.push_back(distance);
    }

    if (distances.back() < length)
        distances.push_back(length);
    return distances;
}

std::vector<BranchFrame> build_branch_frames(
    const DraxulTreeParams& params,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float length,
    float base_radius,
    float tip_radius,
    int depth,
    uint32_t branch_id)
{
    const float trunk_reference = std::max(params.trunk_length * params.overall_scale, 0.1f);
    const float spacing_scale = std::clamp(length / trunk_reference, 0.25f, 1.0f);
    const std::vector<float> distances = build_ring_distances(params, length, spacing_scale);

    std::vector<BranchFrame> frames;
    frames.reserve(distances.size());

    const glm::vec3 base_axis = glm::normalize(direction);
    glm::vec3 basis_u = orthogonal_unit(base_axis);
    const float bend_angle = hash_to_unit_float(params.seed + branch_id, branch_id, 1U) * kTwoPi;
    const glm::vec3 bend_seed_dir = glm::normalize(
        std::cos(bend_angle) * basis_u
        + std::sin(bend_angle) * glm::normalize(glm::cross(base_axis, basis_u)));
    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    const glm::vec3 target_axis = glm::normalize(
        base_axis
        + bend_seed_dir * (params.curvature * (0.7f + 0.15f * static_cast<float>(depth)))
        + world_up * (params.upward_bias * (depth == 0 ? 0.18f : 0.42f))
        - world_up * (params.droop_bias * (depth > 0 ? 0.15f : 0.03f)));

    glm::vec3 center = origin;
    glm::vec3 axis = base_axis;
    float previous_distance = 0.0f;

    for (size_t index = 0; index < distances.size(); ++index)
    {
        const float distance = distances[index];
        const float t = length > 1e-5f ? std::clamp(distance / length, 0.0f, 1.0f) : 1.0f;
        if (index > 0)
        {
            axis = glm::normalize(glm::mix(base_axis, target_axis, std::sin(t * kPi * 0.5f) * t));
            center += axis * (distance - previous_distance);
            basis_u = project_basis(basis_u, axis);
        }
        const glm::vec3 basis_v = glm::normalize(glm::cross(axis, basis_u));
        const float taper_t = std::pow(t, params.taper_power);
        const float radius = std::lerp(base_radius, tip_radius, taper_t);

        frames.push_back(BranchFrame{
            .center = center,
            .axis = axis,
            .basis_u = basis_u,
            .basis_v = basis_v,
            .radius = radius,
            .t = t,
            .distance = distance,
        });
        previous_distance = distance;
    }

    return frames;
}

void append_branch_rings(
    GeometryMesh& mesh,
    const DraxulTreeParams& params,
    const std::vector<BranchFrame>& frames,
    uint32_t branch_id,
    uint32_t& out_ring_start,
    int radial_segments)
{
    out_ring_start = static_cast<uint32_t>(mesh.vertices.size());
    for (size_t ring_index = 0; ring_index < frames.size(); ++ring_index)
    {
        const BranchFrame& frame = frames[ring_index];
        const float color_t = std::clamp(
            (static_cast<float>(branch_id % 11U) * 0.04f) + frame.t * 0.75f,
            0.0f,
            1.0f);
        for (int segment = 0; segment <= radial_segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(radial_segments);
            const float angle = u * kTwoPi;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const glm::vec3 radial = glm::normalize(c * frame.basis_u + s * frame.basis_v);
            const glm::vec3 tangent_dir = glm::normalize(-s * frame.basis_u + c * frame.basis_v);

            GeometryVertex vertex;
            vertex.position = frame.center + radial * frame.radius;
            vertex.normal = radial;
            vertex.tangent = glm::vec4(tangent_dir, 1.0f);
            vertex.color = bark_color_for_vertex(
                params,
                color_t,
                branch_id * 4096U + static_cast<uint32_t>(ring_index),
                static_cast<uint32_t>(segment));
            vertex.uv = { u, frame.t };
            mesh.vertices.push_back(vertex);
        }
    }

    const int ring_vertex_count = radial_segments + 1;
    for (size_t ring_index = 0; ring_index + 1 < frames.size(); ++ring_index)
    {
        const uint32_t row0 = out_ring_start + static_cast<uint32_t>(ring_index * static_cast<size_t>(ring_vertex_count));
        const uint32_t row1 = row0 + static_cast<uint32_t>(ring_vertex_count);
        for (int segment = 0; segment < radial_segments; ++segment)
        {
            const uint32_t a = row0 + static_cast<uint32_t>(segment);
            const uint32_t b = row0 + static_cast<uint32_t>(segment + 1);
            const uint32_t c = row1 + static_cast<uint32_t>(segment + 1);
            const uint32_t d = row1 + static_cast<uint32_t>(segment);

            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }
}

void append_cap(
    GeometryMesh& mesh,
    const DraxulTreeParams& params,
    uint32_t ring_start,
    int radial_segments,
    const glm::vec3& center,
    const glm::vec3& normal,
    float v_coord,
    bool flip_winding = false);

void append_branch(
    GeometryMesh& mesh,
    const DraxulTreeParams& params,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float length,
    float base_radius,
    float tip_radius,
    int depth,
    uint32_t branch_id,
    bool close_base)
{
    if (depth > params.max_branch_depth)
        return;
    if (length <= 0.1f || base_radius <= 0.01f)
        return;

    const std::vector<BranchFrame> frames = build_branch_frames(
        params,
        origin,
        direction,
        length,
        base_radius,
        tip_radius,
        depth,
        branch_id);
    if (frames.size() < 2)
        return;

    const int radial_segments = std::max(params.radial_segments, 3);
    uint32_t ring_start = 0;
    append_branch_rings(mesh, params, frames, branch_id, ring_start, radial_segments);

    if (close_base)
    {
        append_cap(
            mesh,
            params,
            ring_start,
            radial_segments,
            frames.front().center,
            -frames.front().axis,
            0.0f,
            true);
    }

    const uint32_t tip_ring_start = ring_start
        + static_cast<uint32_t>((frames.size() - 1) * static_cast<size_t>(radial_segments + 1));
    append_cap(
        mesh,
        params,
        tip_ring_start,
        radial_segments,
        frames.back().center,
        frames.back().axis,
        1.0f,
        false);

    if (depth >= params.max_branch_depth
        || params.child_branches_max <= 0
        || base_radius < 0.025f * params.overall_scale)
    {
        return;
    }

    const int branch_count = hash_to_int(
        params.seed + branch_id * 17U,
        branch_id,
        7U + static_cast<uint32_t>(depth),
        params.child_branches_min,
        params.child_branches_max);
    if (branch_count <= 0)
        return;

    for (int child_index = 0; child_index < branch_count; ++child_index)
    {
        const float spawn_unit = hash_to_unit_float(
            params.seed + branch_id * 29U,
            static_cast<uint32_t>(child_index),
            19U + static_cast<uint32_t>(depth));
        const float spawn_t = std::lerp(params.branch_spawn_start, params.branch_spawn_end, spawn_unit);
        const size_t frame_index = std::clamp(
            static_cast<size_t>(std::lround(spawn_t * static_cast<float>(frames.size() - 1))),
            static_cast<size_t>(1),
            frames.size() - 2);
        const BranchFrame& frame = frames[frame_index];

        const float azimuth = hash_to_unit_float(
                                  params.seed + branch_id * 43U,
                                  static_cast<uint32_t>(child_index),
                                  31U)
            * kTwoPi;
        const glm::vec3 radial_dir = glm::normalize(
            std::cos(azimuth) * frame.basis_u
            + std::sin(azimuth) * frame.basis_v);
        const glm::vec3 child_origin = frame.center + radial_dir * frame.radius * 0.82f;

        const float axis_weight = std::lerp(0.18f, 0.34f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 101U));
        const float upward = params.upward_bias * std::lerp(0.75f, 1.2f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 121U));
        const float outward = params.outward_bias * std::lerp(0.75f, 1.15f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 151U));
        const float droop = params.droop_bias * std::lerp(0.5f, 1.2f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 171U));
        glm::vec3 child_direction = glm::normalize(
            frame.axis * axis_weight
            + radial_dir * outward
            + glm::vec3(0.0f, 1.0f, 0.0f) * upward
            - glm::vec3(0.0f, 1.0f, 0.0f) * droop);

        const float child_length = length
            * params.branch_length_scale
            * std::lerp(0.72f, 1.1f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 191U));
        const float child_base_radius = frame.radius
            * params.branch_radius_scale
            * std::lerp(0.82f, 1.08f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 211U));
        const float child_tip_radius = std::max(
            child_base_radius
                * std::lerp(0.28f, 0.42f, hash_to_unit_float(params.seed, branch_id, static_cast<uint32_t>(child_index) + 241U)),
            0.006f * params.overall_scale);

        append_branch(
            mesh,
            params,
            child_origin,
            child_direction,
            child_length,
            child_base_radius,
            child_tip_radius,
            depth + 1,
            branch_id * 7U + static_cast<uint32_t>(child_index) + 1U,
            false);
    }
}

void append_cap(
    GeometryMesh& mesh,
    const DraxulTreeParams& params,
    uint32_t ring_start,
    int radial_segments,
    const glm::vec3& center,
    const glm::vec3& normal,
    float v_coord,
    bool flip_winding)
{
    GeometryVertex center_vertex;
    center_vertex.position = center;
    center_vertex.normal = normal;
    center_vertex.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    center_vertex.color = bark_color_for_vertex(params, v_coord, static_cast<uint32_t>(v_coord * 1024.0f), 0);
    center_vertex.uv = { 0.5f, v_coord };

    const uint32_t center_index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(center_vertex);

    for (int segment = 0; segment < radial_segments; ++segment)
    {
        const uint32_t a = ring_start + static_cast<uint32_t>(segment);
        const uint32_t b = ring_start + static_cast<uint32_t>(segment + 1);
        if (flip_winding)
        {
            mesh.indices.push_back(center_index);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a);
        }
        else
        {
            mesh.indices.push_back(center_index);
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
        }
    }
}

} // namespace

DraxulTreeParams make_tree_params_from_age(float age_years)
{
    DraxulTreeParams params;
    const float clamped_age = std::clamp(age_years, 0.5f, 40.0f);
    const float t = (clamped_age - 0.5f) / (40.0f - 0.5f);

    params.age_years = clamped_age;
    params.overall_scale = std::lerp(0.35f, 1.0f, t);
    params.max_height = std::lerp(2.5f, 18.0f, t);
    params.max_canopy_radius = std::lerp(1.0f, 7.0f, t);
    params.radial_segments = std::max(6, static_cast<int>(std::lround(std::lerp(6.0f, 12.0f, t))));
    params.trunk_length = std::lerp(1.5f, 6.0f, t);
    params.trunk_base_radius = std::lerp(0.08f, 0.35f, t);
    params.trunk_tip_radius = std::lerp(0.03f, 0.18f, t);
    params.base_ring_spacing = std::lerp(0.25f, 0.45f, t);
    params.max_branch_depth = std::max(1, static_cast<int>(std::lround(std::lerp(1.0f, 3.0f, t))));
    params.child_branches_min = t < 0.2f ? 0 : 1;
    params.child_branches_max = std::max(params.child_branches_min, static_cast<int>(std::lround(std::lerp(1.0f, 3.0f, t))));
    return params;
}

GeometryMesh generate_draxul_tree(const DraxulTreeParams& input_params)
{
    GeometryMesh mesh;

    DraxulTreeParams params = input_params;
    params.age_years = std::max(params.age_years, 0.5f);
    params.overall_scale = std::max(params.overall_scale, 0.01f);
    params.radial_segments = std::max(params.radial_segments, 3);
    params.trunk_length = std::max(params.trunk_length, 0.25f);
    params.trunk_base_radius = std::max(params.trunk_base_radius, 0.02f);
    params.trunk_tip_radius = std::clamp(params.trunk_tip_radius, 0.01f, params.trunk_base_radius);
    params.base_ring_spacing = std::max(params.base_ring_spacing, 0.05f);
    params.tip_ring_spacing_scale = std::clamp(params.tip_ring_spacing_scale, 0.1f, 1.0f);
    params.taper_power = std::max(params.taper_power, 0.1f);
    params.child_branches_min = std::max(params.child_branches_min, 0);
    params.child_branches_max = std::max(params.child_branches_max, params.child_branches_min);
    params.max_branch_depth = std::max(params.max_branch_depth, 0);
    params.branch_length_scale = std::clamp(params.branch_length_scale, 0.1f, 1.0f);
    params.branch_radius_scale = std::clamp(params.branch_radius_scale, 0.1f, 1.0f);
    params.curvature = std::clamp(params.curvature, 0.0f, 1.0f);

    const float scale = params.overall_scale;
    const float trunk_length = std::min(params.trunk_length * scale, params.max_height);
    const float base_radius = params.trunk_base_radius * scale;
    const float tip_radius = params.trunk_tip_radius * scale;
    mesh.vertices.reserve(8192);
    mesh.indices.reserve(24576);

    append_branch(
        mesh,
        params,
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        trunk_length,
        base_radius,
        tip_radius,
        0,
        1U,
        true);

    return mesh;
}

} // namespace draxul
