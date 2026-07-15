#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;
layout(set = 0, binding = 1) uniform sampler2D lut_sampler;
layout(set = 0, binding = 2) uniform sampler2D bloom_sampler;

layout(push_constant) uniform Push {
    float time;
    float waterwarp;
    float color_enabled;
    float brightness;
    float contrast;
    float saturation;
    vec2 output_size;
    vec4 tint;
    vec4 split_params;
    vec4 split_shadow;
    vec4 split_highlight;
    vec4 lut_params;
    vec4 bloom_final;
} push_data;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 output_size = max(push_data.output_size, vec2(1.0));
    vec2 tc = gl_FragCoord.xy / output_size;
    if (push_data.waterwarp > 0.5) {
        // Match OpenGL GLS_WARP_ENABLE in the full-screen post-process pass.
        tc += 0.0625 * sin(tc.yx * 4.0 + push_data.time);
    }

    vec4 color = texture(scene_sampler, tc);
    if (push_data.bloom_final.w > 0.5) {
        float scene_luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        color.rgb = mix(vec3(scene_luma), color.rgb,
                        push_data.bloom_final.y);
        vec3 bloom = texture(bloom_sampler, tc).rgb;
        float bloom_luma = dot(bloom, vec3(0.2126, 0.7152, 0.0722));
        bloom = mix(vec3(bloom_luma), bloom, push_data.bloom_final.z);
        color.rgb += bloom * push_data.bloom_final.x;
    }
    if (push_data.color_enabled > 0.5) {
        // Keep the OpenGL color-correction order: contrast, brightness,
        // saturation, then tint. Alpha remains the copied scene alpha.
        color.rgb = (color.rgb - vec3(0.5)) * push_data.contrast + vec3(0.5);
        color.rgb += push_data.brightness;
        float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        color.rgb = mix(vec3(luma), color.rgb, push_data.saturation);
        color.rgb *= push_data.tint.rgb;
    }
    if (push_data.split_params.x > 0.0) {
        // Match OpenGL's postfx split-toning order after colour correction.
        float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        float balance = clamp(push_data.split_params.y, -1.0, 1.0);
        float pivot = 0.5 + balance * 0.5;
        float weight = smoothstep(pivot - 0.25, pivot + 0.25, luma);
        vec3 toned = mix(color.rgb * push_data.split_shadow.rgb,
                         color.rgb * push_data.split_highlight.rgb, weight);
        color.rgb = mix(color.rgb, toned, push_data.split_params.x);
    }
    if (push_data.lut_params.x > 0.0 && push_data.lut_params.y > 1.0) {
        // Match OpenGL's 2D NxN LUT strip interpolation after split toning.
        vec3 lut_color = clamp(color.rgb, 0.0, 1.0);
        float size = push_data.lut_params.y;
        float slice = lut_color.b * (size - 1.0);
        float slice0 = floor(slice);
        float slice1 = min(slice0 + 1.0, size - 1.0);
        float t = slice - slice0;
        float u = (lut_color.r * (size - 1.0) + 0.5) / size;
        float v = (lut_color.g * (size - 1.0) + 0.5) / size;
        vec2 uv0;
        vec2 uv1;
        if (push_data.lut_params.z < push_data.lut_params.w) {
            uv0 = vec2((slice0 * size + u) * push_data.lut_params.z,
                       v * push_data.lut_params.w);
            uv1 = vec2((slice1 * size + u) * push_data.lut_params.z,
                       v * push_data.lut_params.w);
        } else {
            uv0 = vec2(u * push_data.lut_params.z,
                       (slice0 * size + v) * push_data.lut_params.w);
            uv1 = vec2(u * push_data.lut_params.z,
                       (slice1 * size + v) * push_data.lut_params.w);
        }
        vec3 graded = mix(texture(lut_sampler, uv0).rgb,
                           texture(lut_sampler, uv1).rgb, t);
        color.rgb = mix(color.rgb, graded, push_data.lut_params.x);
    }
    out_color = color;
}
