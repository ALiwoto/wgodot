// wgodot-changes::file
#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"

class Node;

// Authored in asset-local coordinates, independently of the generated model.
class WGodotMeshCollision {
public:
	static constexpr const char *SOURCE_META = "_edit_mesh_collision_source";
	static String sidecar_path(const String &p_source);
	static String imported_path(const String &p_source);
	static String effective_path(const String &p_source);
	static bool is_superseded(const String &p_path);
	static String import_signature(const String &p_source);
	static bool import_dependency_changed(const String &p_source, uint64_t p_import_time, const Vector<String> &p_dependencies);
	static Error append_to_scene(Node *p_scene, const String &p_source);
};
