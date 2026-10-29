// wgodot-changes::file
#include "mesh.h"

#include "core/object/callable_mp.h"
#include "servers/rendering/rendering_server.h"

void ArrayMesh::_lod_profile_changed() {
	const MeshLODData data = lod_profile.is_valid() ? lod_profile->get_data() : MeshLODData();
	ERR_FAIL_COND_MSG(!data.is_valid(get_surface_count()), "Mesh LOD profile must cover every surface and every resident level.");
	if (mesh.is_valid()) {
		RS::get_singleton()->mesh_set_lod_profile(mesh, data);
	}
	clear_cache();
	emit_changed();
}

void ArrayMesh::set_lod_profile(const Ref<MeshLODProfile> &p_profile) {
	if (p_profile == lod_profile) {
		return;
	}
	ERR_FAIL_COND_MSG(p_profile.is_valid() && !p_profile->get_data().is_valid(get_surface_count()), "Assign the LOD profile after adding its mesh surfaces.");
	if (lod_profile.is_valid()) {
		lod_profile->disconnect_changed(callable_mp(this, &ArrayMesh::_lod_profile_changed));
	}
	lod_profile = p_profile;
	if (lod_profile.is_valid()) {
		lod_profile->connect_changed(callable_mp(this, &ArrayMesh::_lod_profile_changed));
	}
	_lod_profile_changed();
}

bool ArrayMesh::surface_is_base_lod(int p_surface) const {
	ERR_FAIL_INDEX_V(p_surface, get_surface_count(), false);
	if (lod_profile.is_null() || lod_profile->get_data().is_empty()) {
		return true;
	}
	const Vector<int32_t> &levels = lod_profile->get_data().surface_levels;
	ERR_FAIL_INDEX_V(p_surface, levels.size(), false);
	return levels[p_surface] == lod_profile->get_minimum_level();
}
