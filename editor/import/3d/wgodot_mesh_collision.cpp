// wgodot-changes::file
#include "wgodot_mesh_collision.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/packed_scene.h"

String WGodotMeshCollision::sidecar_path(const String &p_source) {
	return p_source.get_basename() + "_collision.tscn";
}

String WGodotMeshCollision::imported_path(const String &p_source) {
	return p_source.get_basename() + "_imported_collision.tscn";
}

String WGodotMeshCollision::effective_path(const String &p_source) {
	const String custom = sidecar_path(p_source);
	return FileAccess::exists(custom) ? custom : imported_path(p_source);
}

bool WGodotMeshCollision::is_superseded(const String &p_path) {
	return p_path.ends_with("_imported_collision.tscn") && FileAccess::exists(p_path.trim_suffix("_imported_collision.tscn") + "_collision.tscn");
}

String WGodotMeshCollision::import_signature(const String &p_source) {
	const String path = effective_path(p_source);
	if (!FileAccess::exists(path)) {
		return String();
	}
	const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	return path + ":" + itos(FileAccess::get_modified_time(path)) + ":" + (file.is_valid() ? itos(file->get_length()) : String("unreadable"));
}

bool WGodotMeshCollision::import_dependency_changed(const String &p_source, uint64_t p_import_time, const Vector<String> &p_dependencies) {
	const String custom = sidecar_path(p_source);
	const String imported = imported_path(p_source);
	const String selected = FileAccess::exists(custom) ? custom : FileAccess::exists(imported) ? imported : String();
	bool had_collision = false;
	bool selected_before = false;
	for (const String &dependency : p_dependencies) {
		// Dependency entries can include a UID and type before the fallback path.
		had_collision |= dependency.ends_with(custom) || dependency.ends_with(imported);
		selected_before |= !selected.is_empty() && dependency.ends_with(selected);
	}
	if (selected.is_empty()) {
		return had_collision;
	}
	return !selected_before || FileAccess::get_modified_time(selected) >= p_import_time;
}

Error WGodotMeshCollision::append_to_scene(Node *p_scene, const String &p_source) {
	const String path = effective_path(p_source);
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
	// Stable across the imported/default and authored variants: scene instances
	// can override the body's collision properties without depending on the source.
	body->set_name("MeshCollision");
	p_scene->add_child(body, true);
	body->set_owner(p_scene);
	return OK;
}
