/*
Copyright (C) 2026 WORR contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

// Native RTX/vkpt indexed RmlUi geometry. Positions are converted to the
// swapchain NDC convention by the renderer queue; texture IDs index the same
// bindless image table as the existing stretch-pic UI pipeline.

#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive    : enable

#include "utils.glsl"

out gl_PerVertex {
	vec4 gl_Position;
};

layout(location = 0) out vec4 color;
layout(location = 1) out flat uint tex_id;
layout(location = 2) out vec2 tex_coord;

struct RmlUiVertex {
	vec2 position;
	vec2 tex_coord;
	uint packed_color;
	uint texture_id;
};

layout(set = 0, binding = 0, std430) readonly buffer RmlUiVertices {
	RmlUiVertex vertices[];
};

void main()
{
	RmlUiVertex vertex = vertices[gl_VertexIndex];
	color = unpackUnorm4x8(vertex.packed_color);
	color.rgb = mix(color.rgb / 12.92,
		pow((color.rgb + 0.055) / 1.055, vec3(2.4)),
		greaterThan(color.rgb, vec3(0.04045)));
	tex_coord = vertex.tex_coord;
	tex_id = vertex.texture_id;
	gl_Position = vec4(vertex.position, 0.0, 1.0);
}
