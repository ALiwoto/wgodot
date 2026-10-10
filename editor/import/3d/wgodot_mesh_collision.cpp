// wgodot-changes::file
#include "wgodot_mesh_collision.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/packed_scene.h"

String WGodotMeshCollision::sidecar_path(const String &p_source) {
	return p_source.get_basename() + "_collision.tscn";
}

Error WGodotMeshCollision::append_to_scene(Node *p_scene, const String &p_source) {
	const String path = sidecar_path(p_source);
	if (!FileAccess::exists(path)) {
		return OK;
	}
	Ref<PackedScene> collision = ResourceLoader::load(path);
	ERR_FAIL_COND_V_MSG(collision.is_null(), ERR_CANT_OPEN, "Cannot load mesh collision sidecar: " + path);
	Node *body = collision->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	ERR_FAIL_NULL_V_MSG(body, ERR_CANT_CREATE, "Cannot instantiate mesh collision sidecar: " + path);
	if (!Object::cast_to<StaticBody3D>(body)) {
		memdelete(body);
		ERR_FAIL_V_MSG(ERR_INVALID_DATA, "Mesh collision sidecar must have a StaticBody3D root: " + path);
	}
	p_scene->add_child(body, true);
	body->set_owner(p_scene);
	return OK;
}
