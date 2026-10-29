// wgodot-changes::file
#include "mesh_instance_3d.h"
#include "servers/rendering/rendering_server.h"

Ref<Material> MeshInstance3D::_effective_surface_override(int p_surface) const {
	const Ref<Material> &material = surface_override_materials[p_surface];
	if (material.is_valid() || surface_override_sources.is_empty()) {
		return material;
	}
	const int source = surface_override_sources[p_surface];
	return source >= 0 ? surface_override_materials[source] : Ref<Material>();
}

void MeshInstance3D::_update_surface_override(int p_surface) {
	const Ref<Material> material = _effective_surface_override(p_surface);
	RS::get_singleton()->instance_set_surface_override_material(get_instance(), p_surface, material.is_valid() ? material->get_rid() : RID());
}
