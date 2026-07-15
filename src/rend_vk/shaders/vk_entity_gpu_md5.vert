#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

struct md5_weight_t {
    vec4 pos_bias;
    uint joint_index;
};

struct md5_joint_t {
    vec4 pos_scale;
    vec4 axis0;
    vec4 axis1;
    vec4 axis2;
};

layout(std430, set = 3, binding = 0) readonly buffer Md5Weights {
    md5_weight_t weights[];
};

layout(std430, set = 3, binding = 1) readonly buffer Md5Joints {
    md5_joint_t joints[];
};

// Static mesh data: bind-pose normal/UV plus its global static weight range.
layout(location = 0) in vec3 in_bind_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in uint in_weight_offset;
layout(location = 3) in uint in_weight_count;

// Current-frame instance data: the CPU resolves the small joint palette while
// the vertex stage performs every weighted position/normal reconstruction.
layout(location = 4) in vec3 in_origin;
layout(location = 5) in uint in_joint_palette_offset;
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

vec3 rotate_by_joint(vec3 value, md5_joint_t joint) {
    return vec3(dot(value, joint.axis0.xyz),
                dot(value, joint.axis1.xyz),
                dot(value, joint.axis2.xyz));
}

void main() {
    vec3 local_pos = vec3(0.0);
    vec3 local_normal = vec3(0.0);
    for (uint i = 0u; i < in_weight_count; i++) {
        md5_weight_t weight = weights[in_weight_offset + i];
        md5_joint_t joint = joints[in_joint_palette_offset + weight.joint_index];
        vec3 rotated_pos = rotate_by_joint(weight.pos_bias.xyz, joint);
        local_pos += weight.pos_bias.w *
            (joint.pos_scale.xyz + joint.pos_scale.w * rotated_pos);
        local_normal += weight.pos_bias.w * rotate_by_joint(in_bind_normal, joint);
    }
    local_pos += in_shell.x * local_normal;

    vec3 world_pos = in_origin +
        local_pos.x * in_scaled_axis0.xyz +
        local_pos.y * in_scaled_axis1.xyz +
        local_pos.z * in_scaled_axis2.xyz;
    vec3 world_normal = normalize(
        local_normal.x * in_normal_axis0.xyz +
        local_normal.y * in_normal_axis1.xyz +
        local_normal.z * in_normal_axis2.xyz);

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
