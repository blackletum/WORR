#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

// Device-local inline-BSP geometry remains in model space. The current frame
// contributes only the model transform and lighting classification, avoiding
// the former CPU expansion of every visible face.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec2 in_lm_uv;
layout(location = 3) in vec3 in_normal;
layout(location = 4) in float in_face_alpha;
layout(location = 5) in uint in_face_flags;
layout(location = 6) in vec3 in_origin;
layout(location = 7) in vec4 in_scaled_axis0;
layout(location = 8) in vec4 in_scaled_axis1;
layout(location = 9) in vec4 in_scaled_axis2;
layout(location = 10) in vec4 in_normal_axis0;
layout(location = 11) in vec4 in_normal_axis1;
layout(location = 12) in vec4 in_normal_axis2;
layout(location = 13) in vec4 in_color;
layout(location = 14) in uint in_entity_flags;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_lm_uv;
layout(location = 2) out vec4 out_color;
layout(location = 3) flat out uint out_flags;
layout(location = 4) out vec3 out_world_pos;
layout(location = 5) out vec3 out_normal;

const uint VK_ENTITY_VERTEX_LIGHTMAP = 4u;

void main() {
    vec3 world_pos = in_origin +
        in_pos.x * in_scaled_axis0.xyz +
        in_pos.y * in_scaled_axis1.xyz +
        in_pos.z * in_scaled_axis2.xyz;
    vec3 world_normal = normalize(
        in_normal.x * in_normal_axis0.xyz +
        in_normal.y * in_normal_axis1.xyz +
        in_normal.z * in_normal_axis2.xyz);

    vec4 clip = push_data.proj * push_data.view * vec4(world_pos, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    out_uv = in_uv;
    out_lm_uv = in_lm_uv;
    out_color = vec4(
        (in_face_flags & VK_ENTITY_VERTEX_LIGHTMAP) != 0u
            ? vec3(1.0) : in_color.rgb,
        min(in_color.a, in_face_alpha));
    out_flags = in_face_flags | in_entity_flags;
    out_world_pos = world_pos;
    out_normal = world_normal;
}
