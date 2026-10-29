// wgodot-changes::file
#include "gltf_mesh_lods.h"
#include <cfloat>

Error gltf_parse_mesh_lods(const Dictionary &p_extras, const Ref<ImporterMesh> &p_mesh) {
	if (!p_extras.has("wgodot_mesh_lods")) {
		return OK;
	}
	const Dictionary description = p_extras["wgodot_mesh_lods"];
	ERR_FAIL_COND_V(!description.has("surface_levels") || !description.has("thresholds") || !description.has("center") || !description.has("radius"), ERR_FILE_CORRUPT);
	const Array source_levels = description["surface_levels"];
	const Array source_thresholds = description["thresholds"];
	const Array center = description["center"];
	ERR_FAIL_COND_V(center.size() != 3, ERR_FILE_CORRUPT);
	Vector<int32_t> surface_levels;
	surface_levels.resize(source_levels.size());
	for (int i = 0; i < source_levels.size(); i++) {
		const double level = source_levels[i];
		ERR_FAIL_COND_V(!Math::is_finite(level) || level < 0 || level >= source_thresholds.size() || level != Math::floor(level), ERR_FILE_CORRUPT);
		surface_levels.write[i] = int32_t(level);
	}
	Vector<Vector2> thresholds;
	thresholds.resize(source_thresholds.size());
	for (int i = 0; i < source_thresholds.size(); i++) {
		const Array pair = source_thresholds[i];
		ERR_FAIL_COND_V(pair.size() != 2, ERR_FILE_CORRUPT);
		thresholds.write[i] = Vector2(pair[0], pair[1]);
	}
	const Vector3 origin(center[0], center[1], center[2]);
	const double radius = description["radius"];
	const double minimum_level = description.get("minimum_level", 0);
	ERR_FAIL_COND_V(!origin.is_finite() || !Math::is_finite(radius) || radius < 0 || radius > FLT_MAX, ERR_FILE_CORRUPT);
	ERR_FAIL_COND_V(!Math::is_finite(minimum_level) || minimum_level < 0 || minimum_level >= thresholds.size() || minimum_level != Math::floor(minimum_level), ERR_FILE_CORRUPT);
	Ref<MeshLODProfile> profile;
	profile.instantiate();
	profile->set_surface_levels(surface_levels);
	profile->set_thresholds(thresholds);
	profile->set_center(origin);
	profile->set_radius(radius);
	profile->set_minimum_level(int(minimum_level));
	ERR_FAIL_COND_V_MSG(!profile->get_data().is_valid(p_mesh->get_surface_count()), ERR_FILE_CORRUPT, "glTF mesh LOD profile does not cover its surfaces.");
	p_mesh->set_lod_profile(profile);
	return OK;
}
