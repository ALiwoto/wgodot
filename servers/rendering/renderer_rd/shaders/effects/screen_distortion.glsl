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

void main() {
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	vec2 size = vec2(textureSize(scene_color, 0));
	vec2 offset = texelFetch(distortion_offsets, pixel, 0).xy;
	vec2 uv = (vec2(pixel) + 0.5) / size + offset;
	vec2 lower = (vec2(params.region.xy) + 0.5) / size;
	vec2 upper = (vec2(params.region.zw) - 0.5) / size;
	// Neutral/cancelled distortion is an exact identity. Out-of-frame samples
	// retain the original pixel rather than stretching the viewport's edges.
	if (any(lessThan(pixel, params.region.xy)) || any(greaterThanEqual(pixel, params.region.zw)) || offset == vec2(0.0) || any(isnan(offset)) || any(isinf(offset)) || any(lessThan(uv, lower)) || any(greaterThan(uv, upper))) {
		color = texelFetch(scene_color, pixel, 0);
	} else {
		color = textureLod(scene_color, uv, 0.0);
	}
}
