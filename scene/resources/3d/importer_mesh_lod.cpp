// wgodot-changes::file
#include "importer_mesh.h"

void ImporterMesh::set_shadow_mesh(const Ref<ImporterMesh> &p_mesh) {
	ERR_FAIL_COND(p_mesh.ptr() == this);
	if (shadow_mesh == p_mesh) {
		return;
	}
	shadow_mesh = p_mesh;
	mesh.unref();
	emit_changed();
}

void ImporterMesh::set_lod_profile(const Ref<MeshLODProfile> &p_profile) {
	ERR_FAIL_COND_MSG(p_profile.is_valid() && !p_profile->is_valid(surfaces.size()), "Assign the LOD profile after adding its mesh surfaces.");
	if (lod_profile == p_profile) {
		return;
	}
	lod_profile = p_profile;
	mesh.unref();
	if (shadow_mesh.is_valid()) {
		shadow_mesh->set_lod_profile(p_profile);
	}
	emit_changed();
}
