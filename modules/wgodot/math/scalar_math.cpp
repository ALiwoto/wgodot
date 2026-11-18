// wgodot-changes::file
#include "scalar_math.h"

#include "core/math/wgodot_scalar_math.h"
#include "core/object/class_db.h"

float WScalarMath::sqrt32(float p_value) {
	return WScalarMath32::sqrt(p_value);
}

float WScalarMath::reciprocal_sqrt_estimate32(float p_value) {
	return WScalarMath32::reciprocal_sqrt_estimate(p_value);
}

void WScalarMath::_bind_methods() {
	ClassDB::bind_static_method("WScalarMath", D_METHOD("sqrt32", "value"), &WScalarMath::sqrt32);
	ClassDB::bind_static_method("WScalarMath", D_METHOD("reciprocal_sqrt_estimate32", "value"), &WScalarMath::reciprocal_sqrt_estimate32);
}
