// wgodot-changes::file
#include "mesh_storage.h"

namespace RendererRD {

void MeshStorage::mesh_set_lod_profile(RID p_mesh, const MeshLODData &p_data) {
	Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	ERR_FAIL_NULL(mesh);
	ERR_FAIL_COND_MSG(!p_data.is_valid(mesh->surface_count), "Mesh LOD profile does not cover the resident geometry.");
	mesh->lod_profile = p_data;
	for (uint32_t surface = 0; surface < mesh->surface_count; surface++) {
		mesh->surfaces[surface]->lod_level = p_data.is_empty() ? -1 : p_data.surface_levels[surface];
	}
	for (MeshInstance *instance : mesh->instances) {
		instance->dirty = true;
	}
	mesh->dependency.changed_notify(Dependency::DEPENDENCY_CHANGED_MESH);
}

const MeshLODData *MeshStorage::mesh_get_lod_profile(RID p_mesh) const {
	const Mesh *mesh = mesh_owner.get_or_null(p_mesh);
	return mesh && !mesh->lod_profile.is_empty() ? &mesh->lod_profile : nullptr;
}

void MeshStorage::mesh_instance_select_lod(RID p_mesh_instance, int p_level) {
	MeshInstance *instance = mesh_instance_owner.get_or_null(p_mesh_instance);
	ERR_FAIL_NULL(instance);
	if (instance->selected_lod != p_level) {
		instance->selected_lod = p_level;
		instance->dirty = true;
	}
}

} // namespace RendererRD
