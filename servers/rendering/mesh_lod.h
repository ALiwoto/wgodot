// wgodot-changes::file
#pragma once

#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "core/math/vector2.h"
#include "core/templates/vector.h"

// Independent surfaces retain their own vertices, skin weights and morphs.
// The ordinary index-only simplifier remains available on meshes without this
// data. Selection never allocates, uploads geometry or modifies scene nodes.
struct MeshLODData {
	Vector<int32_t> surface_levels;
	Vector<Vector2> thresholds; // Screen diameter and hysteresis, in viewport fractions.
	Vector3 center;
	float radius = 0.0f;
	int minimum_level = 0;

	bool is_empty() const { return thresholds.is_empty(); }
	bool is_valid(int p_surface_count) const;
	int select(const Transform3D &p_transform, const Projection &p_projection, const Vector3 &p_camera, float p_bias, int p_previous) const;
};
