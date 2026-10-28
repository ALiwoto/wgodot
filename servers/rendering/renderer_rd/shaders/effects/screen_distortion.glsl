// wgodot-changes::file
#[vertex]
#version 450
#VERSION_DEFINES

void main() {
	vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

#[fragment]
#version 450
#VERSION_DEFINES

layout(set = 0, binding = 0) uniform sampler2D scene_color;
layout(set = 0, binding = 1) uniform sampler2D distortion_offsets;
layout(location = 0) out vec4 color;
layout(push_constant, std430) uniform Params {
	ivec4 region;
} params;

#include "screen_distortion_inc.glsl"

void main() {
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	vec2 size = vec2(textureSize(scene_color, 0));
	vec4 packed_offset = texelFetch(distortion_offsets, pixel, 0);
	vec2 uv;
	if (screen_distortion_uv(pixel, size, params.region, packed_offset, uv)) {
		color = textureLod(scene_color, uv, 0.0);
	} else {
		color = texelFetch(scene_color, pixel, 0);
	}
}
