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

glm::vec3 bark_color_for_vertex(const DraxulTreeParams& params, float t, uint32_t ring_index, uint32_t segment_index)
{
    const glm::vec3 base = glm::mix(params.bark_color_root, params.bark_color_tip, std::clamp(t, 0.0f, 1.0f));
    const float noise = (hash_to_unit_float(params.seed, ring_index, segment_index) * 2.0f - 1.0f) * params.bark_color_noise;
    return glm::clamp(base + glm::vec3(noise), glm::vec3(0.0f), glm::vec3(1.0f));
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

    const float scale = params.overall_scale;
    const float trunk_length = std::min(params.trunk_length * scale, params.max_height);
    const float base_radius = params.trunk_base_radius * scale;
    const float tip_radius = params.trunk_tip_radius * scale;
    const int ring_vertex_count = params.radial_segments + 1;

    std::vector<float> ring_heights;
    ring_heights.reserve(static_cast<size_t>(std::ceil(trunk_length / params.base_ring_spacing)) + 2U);
    ring_heights.push_back(0.0f);

    float height = 0.0f;
    while (height < trunk_length)
    {
        const float t = trunk_length > 1e-5f ? std::clamp(height / trunk_length, 0.0f, 1.0f) : 1.0f;
        const float spacing = params.base_ring_spacing * std::lerp(1.0f, params.tip_ring_spacing_scale, t) * scale;
        height = std::min(height + std::max(spacing, 0.025f), trunk_length);
        if (height > ring_heights.back())
            ring_heights.push_back(height);
    }

    if (ring_heights.back() < trunk_length)
        ring_heights.push_back(trunk_length);

    mesh.vertices.reserve(static_cast<size_t>(ring_heights.size()) * static_cast<size_t>(ring_vertex_count) + 2U);
    mesh.indices.reserve(static_cast<size_t>(ring_heights.size() - 1) * static_cast<size_t>(params.radial_segments) * 6U + static_cast<size_t>(params.radial_segments) * 6U);

    for (size_t ring = 0; ring < ring_heights.size(); ++ring)
    {
        const float y = ring_heights[ring];
        const float t = trunk_length > 1e-5f ? std::clamp(y / trunk_length, 0.0f, 1.0f) : 1.0f;
        const float taper_t = std::pow(t, params.taper_power);
        const float radius = std::lerp(base_radius, tip_radius, taper_t);
        const float v = trunk_length > 1e-5f ? y / trunk_length : 0.0f;

        for (int segment = 0; segment <= params.radial_segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(params.radial_segments);
            const float angle = u * kTwoPi;
            const float c = std::cos(angle);
            const float s = std::sin(angle);
            const glm::vec3 radial{ c, 0.0f, s };
            const glm::vec3 position = radial * radius + glm::vec3(0.0f, y, 0.0f);
            const glm::vec3 tangent_dir = glm::normalize(glm::vec3(-s, 0.0f, c));

            GeometryVertex vertex;
            vertex.position = position;
            vertex.normal = radial;
            vertex.tangent = glm::vec4(tangent_dir, 1.0f);
            vertex.color = bark_color_for_vertex(params, t, static_cast<uint32_t>(ring), static_cast<uint32_t>(segment));
            vertex.uv = { u, v };
            mesh.vertices.push_back(vertex);
        }
    }

    for (size_t ring = 0; ring + 1 < ring_heights.size(); ++ring)
    {
        const uint32_t row0 = static_cast<uint32_t>(ring * static_cast<size_t>(ring_vertex_count));
        const uint32_t row1 = row0 + static_cast<uint32_t>(ring_vertex_count);
        for (int segment = 0; segment < params.radial_segments; ++segment)
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

    append_cap(
        mesh,
        params,
        0,
        params.radial_segments,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f),
        0.0f,
        true);

    const uint32_t tip_ring_start
        = static_cast<uint32_t>((ring_heights.size() - 1) * static_cast<size_t>(ring_vertex_count));
    append_cap(
        mesh,
        params,
        tip_ring_start,
        params.radial_segments,
        glm::vec3(0.0f, trunk_length, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        1.0f,
        false);

    return mesh;
}

} // namespace draxul
