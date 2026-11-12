// wgodot-changes::file
#include "scalar_math.h"

#include "core/object/class_db.h"

#include <cmath>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

float WScalarMath::sqrt32(float p_value) {
	return std::sqrt(p_value);
}

float WScalarMath::reciprocal_sqrt_estimate32(float p_value) {
	// This is intentionally an estimate, not correctly rounded division/sqrt.
	// Its precision depends on the target CPU; callers choose their refinement.
#if defined(__SSE2__) || defined(_M_X64)
	return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(p_value)));
#else
	return 1.0f / std::sqrt(p_value);
#endif
}

void WScalarMath::_bind_methods() {
	ClassDB::bind_static_method("WScalarMath", D_METHOD("sqrt32", "value"), &WScalarMath::sqrt32);
	ClassDB::bind_static_method("WScalarMath", D_METHOD("reciprocal_sqrt_estimate32", "value"), &WScalarMath::reciprocal_sqrt_estimate32);
}
