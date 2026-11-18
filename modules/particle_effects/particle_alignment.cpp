// wgodot-changes::file
#include "particle_motion.h"

#include "core/math/wgodot_scalar_math.h"

Vector3 ParticleMeshProfile3D::velocity_rotation(Vector3 p_direction_velocity) const {
	if (velocity_alignment_mode == VELOCITY_ALIGNMENT_BASIS) {
		const Vector3 direction = p_direction_velocity.normalized();
		const double weight = 1.0 + direction.x;
		const Quaternion orientation = weight < 0.000001 ? Quaternion(0.0, 1.0, 0.0, 0.0) : Quaternion(0.0, -direction.z, direction.y, weight).normalized();
		return Basis(orientation).get_euler(EulerOrder::YZX);
	}

	// Imported Euler-additive effects require their binary32 normalization and
	// pole convention. A Basis round trip changes both near vertical alignment.
	float forward = p_direction_velocity.x;
	float side = p_direction_velocity.z;
	float up = p_direction_velocity.y;
	const float length_squared = (forward * forward + side * side) + up * up;
	if (length_squared == 0.0f) {
		return Vector3();
	}
	const float direction_scale = WScalarMath32::reciprocal_sqrt_refined(length_squared);
	forward *= direction_scale;
	side *= direction_scale;
	up *= direction_scale;

	float weight = 1.0f + forward;
	if (weight < 0.000001f) {
		return Vector3(0.0f, -Math::PI, 0.0f);
	}
	float q_up = -up;
	float q_side = side;
	const float quaternion_scale = WScalarMath32::reciprocal_sqrt_refined(q_side * q_side + (q_up * q_up + weight * weight));
	q_up *= quaternion_scale;
	q_side *= quaternion_scale;
	weight *= quaternion_scale;

	const float singularity = -weight * q_up;
	const float yaw = WScalarMath32::atan2_approx(2.0f * weight * q_side, 1.0f - 2.0f * (q_up * q_up + q_side * q_side));
	float roll;
	float pitch;
	if (singularity > 0.4999995f) {
		pitch = float(Math::PI) * 0.5f;
		roll = yaw;
	} else if (singularity < -0.4999995f) {
		pitch = -float(Math::PI) * 0.5f;
		roll = -yaw;
	} else {
		pitch = WScalarMath32::asin_approx(2.0f * singularity);
		roll = WScalarMath32::atan2_approx(-2.0f * q_up * q_side, 1.0f - 2.0f * q_up * q_up);
	}
	return Vector3(roll, -yaw, pitch);
}
