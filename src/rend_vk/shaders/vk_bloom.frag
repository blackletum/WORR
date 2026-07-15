#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;

layout(push_constant) uniform BloomPush {
    vec4 output_size;
    // x = threshold, y = knee, z = firefly clamp, w = Gaussian sigma.
    vec4 params;
    // x = 0 scene copy/downsample, 1 prefilter, 2 horizontal blur,
    // 3 vertical blur.
    vec4 aux;
} push_data;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 output_size = max(push_data.output_size.xy, vec2(1.0));
    vec2 tc = gl_FragCoord.xy / output_size;

    if (push_data.aux.x < 0.5) {
        // Gameplay DOF starts with the same four-tap downsample as bloom,
        // but retains the complete scene rather than extracting highlights.
        vec2 offset = vec2(0.25) / output_size;
        vec3 scene = vec3(0.0);
        scene += texture(scene_sampler, tc + vec2(-offset.x, -offset.y)).rgb;
        scene += texture(scene_sampler, tc + vec2(-offset.x, offset.y)).rgb;
        scene += texture(scene_sampler, tc + vec2(offset.x, -offset.y)).rgb;
        scene += texture(scene_sampler, tc + vec2(offset.x, offset.y)).rgb;
        out_color = vec4(scene * 0.25, 1.0);
        return;
    }

    if (push_data.aux.x < 1.5) {
        // Match OpenGL's scene-only fallback: downsample four samples,
        // clamp fireflies, then apply its soft threshold/knee extraction.
        vec2 offset = vec2(0.25) / output_size;
        vec3 scene = vec3(0.0);
        scene += texture(scene_sampler, tc + vec2(-offset.x, -offset.y)).rgb;
        scene += texture(scene_sampler, tc + vec2(-offset.x, offset.y)).rgb;
        scene += texture(scene_sampler, tc + vec2(offset.x, -offset.y)).rgb;
        scene += texture(scene_sampler, tc + vec2(offset.x, offset.y)).rgb;
        scene *= 0.25;

        float luma = dot(scene, vec3(0.2126, 0.7152, 0.0722));
        float firefly = push_data.params.z;
        if (firefly > 0.0 && luma > firefly) {
            scene *= firefly / max(luma, 1e-5);
            luma = firefly;
        }
        float threshold = max(push_data.params.x, 0.0);
        float knee = threshold * push_data.params.y + 1e-5;
        float soft = clamp(luma - threshold + knee, 0.0, 2.0 * knee);
        soft = (soft * soft) / (4.0 * knee + 1e-5);
        float contribution = max(luma - threshold, 0.0) + soft;
        out_color = vec4(scene * (contribution / max(luma, 1e-5)), 1.0);
        return;
    }

    // The native downsampled bloom target keeps the OpenGL blur contract
    // separable while deriving its Gaussian kernel from vk_bloom_sigma.
    vec2 texel = 1.0 / vec2(textureSize(scene_sampler, 0));
    vec2 direction = push_data.aux.x < 2.5
        ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    float sigma = max(push_data.params.w, 0.5);
    int radius = min(int(sigma * 2.0 + 0.5), 50);
    vec3 sum = vec3(0.0);
    float weight_sum = 0.0;
    for (int i = -50; i <= 50; i++) {
        if (abs(i) > radius) {
            continue;
        }
        float sample_weight = exp(-float(i * i) / (sigma * sigma));
        sum += texture(scene_sampler, tc + direction * float(i)).rgb *
               sample_weight;
        weight_sum += sample_weight;
    }
    out_color = vec4(sum / max(weight_sum, 1e-5), 1.0);
}
