// wgodot-changes::file
#include "mesh_lod.h"

bool MeshLODData::is_valid(int p_surface_count) const {
	if (is_empty()) {
		return surface_levels.is_empty();
	}
	if (surface_levels.size() != p_surface_count || minimum_level < 0 || minimum_level >= thresholds.size() || !center.is_finite() || !Math::is_finite(radius) || radius < 0.0f) {
		return false;
	}
	for (int i = 0; i < thresholds.size(); i++) {
		if (!thresholds[i].is_finite() || thresholds[i].x < 0.0f || thresholds[i].y < 0.0f || !surface_levels.has(i)) {
			return false;
		}
	}
	for (int level : surface_levels) {
		if (level < 0 || level >= thresholds.size()) {
			return false;
		}
	}
	return true;
}

int MeshLODData::select(const Transform3D &p_transform, const Projection &p_projection, const Vector3 &p_camera, float p_bias, int p_previous) const {
	if (is_empty()) {
		return 0;
	}
	const float projection_scale = MAX(Math::abs(p_projection[0][0]), Math::abs(p_projection[1][1]));
	const Vector3 scale = p_transform.basis.get_scale_abs();
	const float world_radius = radius * scale[scale.max_axis_index()] * MAX(p_bias, 0.0f);
	float diameter_squared = projection_scale * world_radius;
	diameter_squared *= diameter_squared;
	if (!p_projection.is_orthogonal()) {
		diameter_squared /= MAX(p_transform.xform(center).distance_squared_to(p_camera), (real_t)CMP_EPSILON2);
	}
	for (int level = thresholds.size() - 1; level > minimum_level; level--) {
		const float threshold = thresholds[level].x + (level <= p_previous ? thresholds[level].y : 0.0f);
		if (threshold * threshold > diameter_squared) {
			return level;
		}
	}
	return minimum_level;
}
