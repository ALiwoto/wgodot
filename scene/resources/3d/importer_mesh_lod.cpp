// wgodot-changes::file
#include "importer_mesh.h"

void ImporterMesh::set_lod_profile(const Ref<MeshLODProfile> &p_profile) {
	ERR_FAIL_COND_MSG(p_profile.is_valid() && !p_profile->get_data().is_valid(surfaces.size()), "Assign the LOD profile after adding its mesh surfaces.");
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
