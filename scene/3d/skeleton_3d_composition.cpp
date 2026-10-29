// wgodot-changes::file
#include "skeleton_3d.h"

namespace {

// Mirrored TRS composition extracts a rotation from individually normalized
// axes, which need not be orthogonal. Basis::get_quaternion() deliberately
// rejects that input in math-check builds; do not use Gram-Schmidt here.
Quaternion rotation_from_axes(const Basis &p_basis) {
	const real_t trace = p_basis[0][0] + p_basis[1][1] + p_basis[2][2];
	Quaternion result;
	if (trace > 0) {
		const real_t root = Math::sqrt(trace + 1);
		const real_t factor = real_t(0.5) / root;
		result = Quaternion((p_basis[2][1] - p_basis[1][2]) * factor,
				(p_basis[0][2] - p_basis[2][0]) * factor,
				(p_basis[1][0] - p_basis[0][1]) * factor, root * real_t(0.5));
	} else {
		int i = 0;
		if (p_basis[1][1] > p_basis[i][i]) {
			i = 1;
		}
		if (p_basis[2][2] > p_basis[i][i]) {
			i = 2;
		}
		const int j = (i + 1) % 3;
		const int k = (i + 2) % 3;
		const real_t root = Math::sqrt(p_basis[i][i] - p_basis[j][j] - p_basis[k][k] + 1);
		const real_t factor = real_t(0.5) / root;
		result[i] = root * real_t(0.5);
		result[j] = (p_basis[j][i] + p_basis[i][j]) * factor;
		result[k] = (p_basis[k][i] + p_basis[i][k]) * factor;
		result.w = (p_basis[k][j] - p_basis[j][k]) * factor;
	}
	return result.normalized();
}

bool has_negative_scale(const Vector3 &p_scale) {
	return p_scale.x < 0 || p_scale.y < 0 || p_scale.z < 0;
}

} // namespace

void SkeletonPoseComponents::from_basis(const Basis &p_basis) {
	scale = p_basis.get_scale();
	// A collapsed axis cannot retain a matrix-only rotation. This also keeps
	// disabled zero-scale bones finite when read through the rest/override API.
	if (scale.x == 0 || scale.y == 0 || scale.z == 0) {
		rotation = Quaternion();
	} else {
		rotation = p_basis.get_rotation_quaternion();
	}
}

Transform3D SkeletonPoseComponents::compose(const Transform3D &p_parent, const SkeletonPoseComponents &p_parent_components,
		const Vector3 &p_position, const Quaternion &p_rotation, const Vector3 &p_scale) {
	scale = p_parent_components.scale * p_scale;
	rotation = p_parent_components.rotation * p_rotation;
	if (has_negative_scale(p_parent_components.scale) || has_negative_scale(p_scale)) {
		from_scaled_axes(p_parent.basis * Basis(p_rotation).scaled_local(p_scale), scale);
	}
	Basis basis;
	basis.set_quaternion_scale(rotation, scale);
	return Transform3D(basis, p_parent.xform(p_position));
}

void SkeletonPoseComponents::from_scaled_axes(Basis p_basis, const Vector3 &p_scale) {
	scale = p_scale;
	for (int axis = 0; axis < 3; axis++) {
		const Vector3 column = p_basis.get_column(axis);
		const real_t length_squared = column.length_squared();
		if (length_squared <= real_t(1e-8)) {
			rotation = Quaternion();
			return;
		}
		p_basis.set_column(axis, column * (scale[axis] < 0 ? -1 : 1) / Math::sqrt(length_squared));
	}
	rotation = rotation_from_axes(p_basis);
}

Transform3D SkeletonPoseComponents::relative_to(const Transform3D &p_transform, const Transform3D &p_parent,
		const SkeletonPoseComponents &p_parent_components) {
	const Vector3 original_scale = scale;
	Vector3 reciprocal;
	for (int axis = 0; axis < 3; axis++) {
		reciprocal[axis] = p_parent_components.scale[axis] == 0 ? 0 : 1 / p_parent_components.scale[axis];
	}
	scale *= reciprocal;
	rotation = p_parent_components.rotation.inverse() * rotation;
	if (has_negative_scale(original_scale) || has_negative_scale(p_parent_components.scale)) {
		const Basis inverse = Basis(p_parent_components.rotation).transposed().scaled(reciprocal);
		from_scaled_axes(inverse * p_transform.basis, scale);
	}
	Basis basis;
	basis.set_quaternion_scale(rotation, scale);
	return Transform3D(basis, p_parent_components.rotation.xform_inv(p_transform.origin - p_parent.origin) * reciprocal);
}

void Skeleton3D::set_pose_composition(PoseComposition p_mode) {
	ERR_FAIL_COND(p_mode < POSE_COMPOSITION_AFFINE || p_mode > POSE_COMPOSITION_COMPONENTS);
	if (pose_composition == p_mode) {
		return;
	}
	pose_composition = p_mode;
	composition_cache.resize(p_mode == POSE_COMPOSITION_COMPONENTS ? bones.size() : 0);
	rest_dirty = true;
	_make_bone_global_poses_dirty();
	_make_dirty();
}

Skeleton3D::PoseComposition Skeleton3D::get_pose_composition() const {
	return pose_composition;
}

void Skeleton3D::_update_component_rest(int p_bone) const {
	Bone &bone = bones[p_bone];
	SkeletonPoseComponents &result = composition_cache[p_bone].rest;
	SkeletonPoseComponents local;
	local.from_basis(bone.rest.basis);
	if (bone.parent >= 0) {
		bone.global_rest = result.compose(bones[bone.parent].global_rest, composition_cache[bone.parent].rest,
				bone.rest.origin, local.rotation, local.scale);
	} else {
		result = local;
		bone.global_rest = bone.rest;
	}
}

void Skeleton3D::_update_component_pose(int p_bone) const {
	Bone &bone = bones[p_bone];
	SkeletonCompositionCache &cache = composition_cache[p_bone];
	const bool enabled = bone.enabled && !show_rest_only;
	SkeletonPoseComponents local;
	Vector3 position;
	if (enabled) {
		// Modifier backup/restore also retains the local matrix cache.
		bone.update_pose_cache();
		local.rotation = bone.pose_rotation;
		local.scale = bone.pose_scale;
		position = bone.pose_position;
	} else {
		local.from_basis(bone.rest.basis);
		position = bone.rest.origin;
	}
	if (bone.parent >= 0) {
		bone.global_pose = cache.pose.compose(bones[bone.parent].global_pose, composition_cache[bone.parent].pose,
				position, local.rotation, local.scale);
	} else {
		cache.pose = local;
		bone.global_pose.basis.set_quaternion_scale(local.rotation, local.scale);
		bone.global_pose.origin = position;
	}
#ifndef DISABLE_DEPRECATED
	cache.has_override = bone.parent >= 0 && composition_cache[bone.parent].has_override;
	if (cache.has_override) {
		bone.pose_global_no_override = cache.no_override.compose(bones[bone.parent].pose_global_no_override,
				composition_cache[bone.parent].no_override, position, local.rotation, local.scale);
	} else {
		cache.no_override = cache.pose;
		bone.pose_global_no_override = bone.global_pose;
	}
	if (bone.global_pose_override_amount >= CMP_EPSILON) {
		bone.global_pose = bone.global_pose.interpolate_with(bone.global_pose_override, bone.global_pose_override_amount);
		cache.pose.from_basis(bone.global_pose.basis);
		cache.has_override = true;
	}
#endif
	bone_global_pose_dirty[bone.nested_set_offset] = false;
}
