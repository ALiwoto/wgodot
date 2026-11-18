// wgodot-changes::file
#include "wgodot_scalar_math.h"

#include <cmath>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

float WScalarMath32::sqrt(float p_value) {
	return std::sqrt(p_value);
}

float WScalarMath32::reciprocal_sqrt_estimate(float p_value) {
	// The CPU estimate is deliberately distinct from correctly rounded sqrt.
#if defined(__SSE2__) || defined(_M_X64)
	return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(p_value)));
#else
	return 1.0f / std::sqrt(p_value);
#endif
}

float WScalarMath32::reciprocal_sqrt_refined(float p_value) {
	float estimate = reciprocal_sqrt_estimate(p_value);
	const float half_input = p_value * 0.5f;
	for (int iteration = 0; iteration < 2; ++iteration) {
		const float squared = estimate * estimate;
		const float correction = 0.5f - half_input * squared;
		estimate = estimate + estimate * correction;
	}
	return estimate;
}

float WScalarMath32::atan2_approx(float p_y, float p_x) {
	const float abs_y = std::abs(p_y);
	const float abs_x = std::abs(p_x);
	const bool swap = abs_y > abs_x;
	const float maximum = swap ? abs_y : abs_x;
	if (maximum == 0.0f) {
		return 0.0f;
	}
	const float ratio = (swap ? abs_x : abs_y) / maximum;
	const float squared = ratio * ratio;
	float result = 0.0072128853633444123f;
	result = result * squared - 0.035059680836411644f;
	result = result * squared + 0.08167588285994043f;
	result = result * squared - 0.13374657325451267f;
	result = result * squared + 0.19856563505717162f;
	result = result * squared - 0.3332499857920217f;
	result = (result * squared + 1.0f) * ratio;
	if (swap) {
		result = 1.5707963267948966f - result;
	}
	if (p_x < 0.0f) {
		result = 3.1415926535897932f - result;
	}
	return p_y < 0.0f ? -result : result;
}

float WScalarMath32::asin_approx(float p_value) {
	const float magnitude = std::abs(p_value);
	const float root = std::sqrt(std::fmax(0.0f, 1.0f - magnitude));
	float result = -0.0012624911f;
	result = result * magnitude + 0.0066700901f;
	result = result * magnitude - 0.0170881256f;
	result = result * magnitude + 0.0308918810f;
	result = result * magnitude - 0.0501743046f;
	result = result * magnitude + 0.0889789874f;
	result = result * magnitude - 0.2145988016f;
	result = (result * magnitude + 1.5707963050f) * root;
	return p_value >= 0.0f ? 1.5707963050f - result : result - 1.5707963050f;
}
