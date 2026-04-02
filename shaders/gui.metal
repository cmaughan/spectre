#include <metal_stdlib>
using namespace metal;

// --- Tooltip overlay ---

struct TooltipUniforms
{
    float4 rect; // x, y, width, height in pixels
    float4 viewport; // viewport_width, viewport_height, 0, 0
};

struct TooltipVertexOut
{
    float4 position [[position]];
    float2 uv;
};

vertex TooltipVertexOut tooltip_vertex(
    uint vid [[vertex_id]],
    constant TooltipUniforms& tooltip [[buffer(0)]])
{
    const float2 corners[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    constexpr uint indices[6] = { 0, 1, 2, 0, 2, 3 };
    const float2 corner = corners[indices[vid]];

    const float2 pixel_pos = tooltip.rect.xy + corner * tooltip.rect.zw;
    float2 ndc = pixel_pos / tooltip.viewport.xy * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    TooltipVertexOut out;
    out.position = float4(ndc, 0.0f, 1.0f);
    out.uv = corner;
    return out;
}

fragment float4 tooltip_fragment(
    TooltipVertexOut in [[stage_in]],
    texture2d<float> tooltipTex [[texture(0)]],
    sampler tooltipSampler [[sampler(0)]])
{
    return tooltipTex.sample(tooltipSampler, in.uv);
}
