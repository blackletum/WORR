#version 450

// The native sky-array path has the same fog behaviour as the regular world
// shader, but samples the six compatible sky faces from one 2D array. The
// first ten vec4 values deliberately retain the ShadowPages UBO prefix so the
// set=2 binding remains ABI-compatible with vk_shadow.c.
#define VK_WORLD_VERTEX_SKY 128u
#define VK_FOG_SKY 4u

layout(set = 0, binding = 0) uniform sampler2DArray tex_sampler;
layout(std140, set = 2, binding = 1) uniform ShadowPages {
    vec4 shadow_global;
    vec4 shadow_sun;
    vec4 shadow_moment_tuning;
    vec4 shadow_glowmap_tuning;
    vec4 shadow_dlight_count;
    vec4 view_origin;
    vec4 shadow_fog_color_density;
    vec4 shadow_heightfog_start;
    vec4 shadow_heightfog_end;
    vec4 shadow_fog_params;
};

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_lm_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) flat in uint in_flags;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = texture(tex_sampler, vec3(in_uv, in_lm_uv.x)) * in_color;
    uint fog_flags = uint(shadow_fog_params.w + 0.5);
    if ((in_flags & VK_WORLD_VERTEX_SKY) != 0u &&
        (fog_flags & VK_FOG_SKY) != 0u) {
        color.rgb = mix(color.rgb, shadow_fog_color_density.rgb,
                        shadow_fog_params.z);
    }
    out_color = color;
}
