// wgodot-changes::file
#pragma once

#include "core/math/transform_3d.h"

// Cached separately from affine matrices so inherited nonuniform scale does
// not turn into shear when a child rotates. Allocated only by opted-in rigs.
struct SkeletonPoseComponents {
	Quaternion rotation;
	Vector3 scale = Vector3(1, 1, 1);

	void from_basis(const Basis &p_basis);
	void from_scaled_axes(Basis p_basis, const Vector3 &p_scale);
	Transform3D relative_to(const Transform3D &p_transform, const Transform3D &p_parent,
			const SkeletonPoseComponents &p_parent_components);
	Transform3D compose(const Transform3D &p_parent, const SkeletonPoseComponents &p_parent_components,
			const Vector3 &p_position, const Quaternion &p_rotation, const Vector3 &p_scale);
};

struct SkeletonCompositionCache {
	SkeletonPoseComponents pose;
	SkeletonPoseComponents rest;
#ifndef DISABLE_DEPRECATED
	SkeletonPoseComponents no_override;
	bool has_override = false;
#endif
};
