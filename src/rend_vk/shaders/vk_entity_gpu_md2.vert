#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

// Two offsets into the immutable per-model frame stream provide native MD2
// interpolation without generating a transformed vertex stream on the CPU.
layout(location = 0) in vec3 in_new_pos;
layout(location = 1) in vec3 in_new_normal;
layout(location = 2) in vec3 in_old_pos;
layout(location = 3) in vec3 in_old_normal;
layout(location = 4) in vec2 in_uv;

// Binding three is a compact current-frame instance stream. The CPU retains
// light sampling and entity classification; interpolation and object-space to
// world-space transformation move to the native Vulkan vertex stage.
layout(location = 5) in vec4 in_origin_frontlerp;
layout(location = 6) in vec4 in_scaled_axis0;
layout(location = 7) in vec4 in_scaled_axis1;
layout(location = 8) in vec4 in_scaled_axis2;
layout(location = 9) in vec4 in_normal_axis0;
layout(location = 10) in vec4 in_normal_axis1;
layout(location = 11) in vec4 in_normal_axis2;
layout(location = 12) in vec4 in_shell;
layout(location = 13) in vec4 in_color;
layout(location = 14) in uint in_flags;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_lm_uv;
layout(location = 2) out vec4 out_color;
layout(location = 3) flat out uint out_flags;
layout(location = 4) out vec3 out_world_pos;
layout(location = 5) out vec3 out_normal;

void main() {
    float frontlerp = clamp(in_origin_frontlerp.w, 0.0, 1.0);
    vec3 local_normal = mix(in_old_normal, in_new_normal, frontlerp);
    vec3 local_pos = mix(in_old_pos, in_new_pos, frontlerp) +
        local_normal * in_shell.x;

    vec3 world_pos = in_origin_frontlerp.xyz +
        local_pos.x * in_scaled_axis0.xyz +
        local_pos.y * in_scaled_axis1.xyz +
        local_pos.z * in_scaled_axis2.xyz;
    vec3 world_normal = local_normal.x * in_normal_axis0.xyz +
        local_normal.y * in_normal_axis1.xyz +
        local_normal.z * in_normal_axis2.xyz;
    world_normal = normalize(world_normal);

    vec4 clip = push_data.proj * push_data.view * vec4(world_pos, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    out_uv = in_uv;
    out_lm_uv = vec2(0.0);
    out_color = in_color;
    out_flags = in_flags;
    out_world_pos = world_pos;
    out_normal = world_normal;
}
