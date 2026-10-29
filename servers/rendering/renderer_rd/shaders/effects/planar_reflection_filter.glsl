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
layout(set = 0, binding = 1) uniform sampler2D scene_depth;
layout(location = 0) out vec4 color;
layout(push_constant, std430) uniform Params {
	mat4 inverse_projection;
	vec4 plane;
	vec4 dimensions;
	vec4 filter;
} params;

float linear_depth(vec2 uv) {
	float z = textureLod(scene_depth, uv, 0.0).r;
	vec4 position = params.inverse_projection * vec4(uv * 2.0 - 1.0, z, 1.0);
	return -position.z / max(position.w, 1e-8);
}

void main() {
	vec2 uv = gl_FragCoord.xy / params.dimensions.zw;
	float radius = params.filter.x;
	if (radius <= 0.0) {
		color = textureLod(scene_color, uv, 0.0);
		return;
	}
	vec4 view_point = params.inverse_projection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
	vec3 position = view_point.xyz / view_point.w;
	vec3 origin = params.filter.z > 0.0 ? vec3(position.xy, 0.0) : vec3(0.0);
	vec3 direction = params.filter.z > 0.0 ? vec3(0.0, 0.0, -1.0) : normalize(position);
	float denominator = dot(params.plane.xyz, direction);
	if (abs(denominator) < 1e-8) {
		color = textureLod(scene_color, uv, 0.0);
		return;
	}
	float plane_distance = -(dot(params.plane.xyz, origin) + params.plane.w) / denominator;
	float center_distance = max(linear_depth(uv) - plane_distance, 0.0);
	vec4 sum = vec4(0.0);
	float weights = 0.0;
	for (int side = 0; side < 2; side++) {
		for (float y = side == 0 ? 0.0 : -2.0; abs(y) < radius; y += side == 0 ? 2.0 : -2.0) {
			vec2 sample_uv = uv + vec2(0.0, y / params.dimensions.y);
			float distance = max(linear_depth(sample_uv) - plane_distance, 0.0);
			float sigma = max(radius * clamp(distance * params.filter.y, 0.0, 1.0), 1.0);
			float weight = exp(-y * y / (2.0 * sigma * sigma)) / sigma;
			if (side == 1) {
				if (distance >= center_distance * 2.0 || sample_uv.y <= 0.5 / params.dimensions.y) {
					continue;
				}
			} else {
				if (sample_uv.y >= 1.0 - 0.5 / params.dimensions.y) {
					continue;
				}
				sample_uv.y += 0.5 / params.dimensions.y;
			}
			sum += max(textureLod(scene_color, sample_uv, 0.0), vec4(0.0)) * weight;
			weights += weight;
		}
	}
	color = weights > 0.0 ? sum / weights : textureLod(scene_color, uv, 0.0);
}
