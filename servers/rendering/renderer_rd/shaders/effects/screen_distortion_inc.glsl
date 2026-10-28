// wgodot-changes::file
#ifndef SCREEN_DISTORTION_INC_GLSL
#define SCREEN_DISTORTION_INC_GLSL

// Color and any associated screen-space data must use the same warp and
// out-of-frame rejection. Returns false for an exact, unfiltered identity.
bool screen_distortion_uv(ivec2 pixel, vec2 size, ivec4 region, vec4 packed_offset, out vec2 uv) {
#ifdef DISTORTION_PACKED_RANGE
	vec2 offset = (packed_offset.xy - packed_offset.zw) * DISTORTION_PACKED_RANGE;
#else
	vec2 offset = packed_offset.xy;
#endif
	vec2 original_uv = (vec2(pixel) + 0.5) / size;
	uv = original_uv + offset;
	vec2 lower = (vec2(region.xy) + 0.5) / size;
	vec2 upper = (vec2(region.zw) - 0.5) / size;
	if (any(lessThan(pixel, region.xy)) || any(greaterThanEqual(pixel, region.zw)) || offset == vec2(0.0) || any(isnan(offset)) || any(isinf(offset)) || any(lessThan(uv, lower)) || any(greaterThan(uv, upper))) {
		uv = original_uv;
		return false;
	}
	return true;
}

#endif
