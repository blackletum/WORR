#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 clip = push_data.proj * push_data.view * vec4(in_pos, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    out_color = in_color;
}
