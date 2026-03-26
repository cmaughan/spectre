#version 450

layout(location = 0) in vec3 in_normal_ws;
layout(location = 1) in vec3 in_base_color;

layout(location = 0) out vec4 out_material;    // RG = octahedral normal, B = roughness, A = specular
layout(location = 1) out vec4 out_base_color;  // RGB = albedo, A = metallic
layout(location = 2) out vec4 out_ao;          // R = ambient occlusion, GBA = reserved

// Octahedral encoding: unit normal -> [0,1]^2
// Reference: "Survey of Efficient Representations for Independent Unit Vectors" (Cigolle et al. 2014)
vec2 oct_encode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * mix(vec2(-1.0), vec2(1.0), greaterThanEqual(n.xy, vec2(0.0)));
    return n.xy * 0.5 + 0.5;
}

void main()
{
    const float roughness = 0.5;
    const float specular = 0.5;
    const float metallic = 0.0;
    const float ao = 1.0;
    out_material = vec4(oct_encode(normalize(in_normal_ws)), roughness, specular);
    out_base_color = vec4(in_base_color, 1.0);
    out_ao = vec4(ao, 0.0, 0.0, 1.0);
}
