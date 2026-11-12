// wgodot-changes::file
#include "wgodot_transform3d.h"

#include "core/templates/hashfuncs.h"

uint32_t WVector3d::hash() const {
	uint32_t result = hash_murmur3_one_double(x);
	result = hash_murmur3_one_double(y, result);
	result = hash_murmur3_one_double(z, result);
	return hash_fmix32(result);
}

WVector3d::operator String() const {
	return "(" + String::num_scientific(x) + ", " + String::num_scientific(y) + ", " + String::num_scientific(z) + ")";
}

namespace {
WVector3d rotate(const Quaternion &p_rotation, const WVector3d &p_value) {
	const WVector3d axis(p_rotation.x, p_rotation.y, p_rotation.z);
	const WVector3d twice_cross = axis.cross(p_value) * 2.0;
	return p_value + twice_cross * p_rotation.w + axis.cross(twice_cross);
}
} // namespace

WVector3d WTransform3d::xform(const WVector3d &p_value) const {
	return rotate(rotation, p_value * WVector3d(scale)) + origin;
}

WVector3d WTransform3d::xform_inv(const WVector3d &p_value, double p_tolerance) const {
	const Vector3 reciprocal(
			Math::abs(scale.x) <= p_tolerance ? 0.0f : 1.0f / scale.x,
			Math::abs(scale.y) <= p_tolerance ? 0.0f : 1.0f / scale.y,
			Math::abs(scale.z) <= p_tolerance ? 0.0f : 1.0f / scale.z);
	return rotate(rotation.inverse(), p_value - origin) * WVector3d(reciprocal);
}

bool WTransform3d::is_same(const WTransform3d &p_value) const {
	for (int i = 0; i < 4; i++) {
		if (!Math::is_same(rotation[i], p_value.rotation[i])) {
			return false;
		}
	}
	for (int i = 0; i < 3; i++) {
		if (!Math::is_same(scale[i], p_value.scale[i])) {
			return false;
		}
	}
	return origin.is_same(p_value.origin);
}

uint32_t WTransform3d::hash() const {
	uint32_t result = origin.hash();
	result = hash_murmur3_one_real(rotation.x, result);
	result = hash_murmur3_one_real(rotation.y, result);
	result = hash_murmur3_one_real(rotation.z, result);
	result = hash_murmur3_one_real(rotation.w, result);
	result = hash_murmur3_one_real(scale.x, result);
	result = hash_murmur3_one_real(scale.y, result);
	result = hash_murmur3_one_real(scale.z, result);
	return hash_fmix32(result);
}

WTransform3d::operator String() const {
	return "[R: " + String(rotation) + ", O: " + String(origin) + ", S: " + String(scale) + "]";
}
