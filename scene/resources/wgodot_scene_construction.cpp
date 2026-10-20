// wgodot-changes::file
#include "wgodot_scene_construction.h"

#include "packed_scene.h"

Ref<Resource> WGodotSceneContext::local_resource(const Ref<Resource> &p_resource, const Ref<Resource> &p_existing) {
	if (inherited) {
		return SceneState::get_remap_resource(p_resource, resources, p_existing, base);
	}
	auto &remap = resources[base];
	if (const Ref<Resource> *existing = remap.getptr(p_resource)) {
		return *existing;
	}
	Ref<Resource> result;
	if (main_edit) {
		p_resource->configure_for_local_scene(base, remap);
		result = p_resource;
	} else {
		result = p_resource->duplicate_for_local_scene(base, remap);
	}
	remap.insert(p_resource, result);
	return result;
}
