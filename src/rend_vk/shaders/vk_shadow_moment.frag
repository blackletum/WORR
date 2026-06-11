#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
    float filter_mode;
    float evsm_exponent;
    vec2 pad;
} push_data;

layout(location = 0) out vec4 out_moment;

void main() {
    float d = clamp(gl_FragCoord.z, 0.0, 1.0);
    if (int(push_data.filter_mode + 0.5) == 3) {
        float w = exp(min(push_data.evsm_exponent * d, push_data.evsm_exponent));
        out_moment = vec4(w, w * w, 0.0, 1.0);
    } else {
        out_moment = vec4(d, d * d, 0.0, 1.0);
    }
}
