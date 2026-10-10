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
	static Error append_to_scene(Node *p_scene, const String &p_source);
};
