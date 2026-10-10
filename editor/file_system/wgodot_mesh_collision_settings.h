// wgodot-changes::file
#pragma once

#include "core/io/config_file.h"

// Authoring decisions belong to the source's .import file, never the runtime mesh.
struct WGodotMeshCollisionSettings {
	static constexpr const char *SECTION = "editor_mesh_collision";
	static constexpr const char *FILE_KEY = "not_needed";
	static constexpr const char *MESHES_KEY = "not_needed_meshes";

	bool available = false;
	bool file_not_needed = false;
	PackedStringArray meshes_not_needed;

	static WGodotMeshCollisionSettings load(const String &p_source);
	static String import_metadata(const Ref<ConfigFile> &p_config);
	static void strip_export_metadata(const Ref<ConfigFile> &p_config);
};
