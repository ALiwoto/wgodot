// wgodot-changes::file
#pragma once

#include "core/math/transform_3d.h"
#include "core/math/wgodot_vector3d.h"

// Quaternion/scale retain real_t precision; only the translation needs the
// larger world-coordinate range. Components retain their independent values.
struct [[nodiscard]] alignas(8) WTransform3d {
	Quaternion rotation;
	WVector3d origin;
	Vector3 scale = Vector3(1, 1, 1);

	WTransform3d() = default;
	WTransform3d(const Quaternion &p_rotation, const WVector3d &p_origin, const Vector3 &p_scale) : rotation(p_rotation), origin(p_origin), scale(p_scale) {
	}

	bool operator==(const WTransform3d &p_value) const {
		return rotation == p_value.rotation && origin == p_value.origin && scale == p_value.scale;
	}
	bool operator!=(const WTransform3d &p_value) const {
		return !(*this == p_value);
	}
	bool is_same(const WTransform3d &p_value) const;
	bool is_finite() const {
		return rotation.is_finite() && origin.is_finite() && scale.is_finite();
	}
	WVector3d xform(const WVector3d &p_value) const;
	WVector3d xform_inv(const WVector3d &p_value, double p_tolerance = 0.0) const;
	Transform3D to_transform3d() const {
		return Transform3D(Basis(rotation).scaled_local(scale), origin.to_vector3());
	}
	uint32_t hash() const;
	operator String() const;
};
