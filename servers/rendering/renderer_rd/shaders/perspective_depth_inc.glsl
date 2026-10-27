// wgodot-changes::file
#ifndef WGODOT_PERSPECTIVE_DEPTH_INC
#define WGODOT_PERSPECTIVE_DEPTH_INC

// Reversed-Z, including the infinite-far limit (1 / infinity = 0).
// The finite reciprocal floor represents cleared background without infinities
// propagating into blur weights, temporal differences or position arithmetic.
float perspective_view_depth(float depth, float z_near, float z_far) {
	float inverse_far = 1.0 / z_far;
	return 1.0 / max(mix(inverse_far, 1.0 / z_near, depth), 1e-20);
}

float perspective_device_depth(float view_depth, float z_near, float z_far) {
	float inverse_far = 1.0 / z_far;
	float reversed_depth = (1.0 / view_depth - inverse_far) / (1.0 / z_near - inverse_far);
	return reversed_depth;
}

#endif
