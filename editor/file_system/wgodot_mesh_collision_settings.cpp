// wgodot-changes::file
#include "wgodot_mesh_collision_settings.h"

#include "editor/file_system/editor_file_system.h"

WGodotMeshCollisionSettings WGodotMeshCollisionSettings::load(const String &p_source) {
	WGodotMeshCollisionSettings settings;
	if (p_source.is_empty() || !FileAccess::exists(p_source + ".import")) {
		return settings;
	}
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(p_source + ".import") != OK) {
		return settings;
	}
	settings.available = true;
	settings.file_not_needed = config->get_value(SECTION, FILE_KEY, false);
	settings.meshes_not_needed = config->get_value(SECTION, MESHES_KEY, PackedStringArray());
	return settings;
}

String WGodotMeshCollisionSettings::import_metadata(const Ref<ConfigFile> &p_config) {
	if (!p_config->has_section(SECTION)) {
		return String();
	}
	Ref<ConfigFile> metadata;
	metadata.instantiate();
	for (const String &key : p_config->get_section_keys(SECTION)) {
		metadata->set_value(SECTION, key, p_config->get_value(SECTION, key));
	}
	return "\n" + metadata->encode_to_text();
}

void WGodotMeshCollisionSettings::strip_export_metadata(const Ref<ConfigFile> &p_config) {
	if (p_config->has_section(SECTION)) {
		p_config->erase_section(SECTION);
	}
}

Error EditorFileSystem::set_mesh_collision_not_needed(const String &p_source, const String &p_mesh, bool p_not_needed) {
	ERR_FAIL_COND_V(is_scanning() || is_importing(), ERR_BUSY);
	EditorFileSystemDirectory *directory = nullptr;
	int index = -1;
	ERR_FAIL_COND_V(!_find_file(p_source, &directory, index), ERR_FILE_NOT_FOUND);
	const String path = p_source + ".import";
	const String previous_md5 = FileAccess::get_md5(path);
	Ref<ConfigFile> config;
	config.instantiate();
	Error error = config->load(path);
	ERR_FAIL_COND_V(error != OK, error);

	using Settings = WGodotMeshCollisionSettings;
	if (p_mesh.is_empty()) {
		config->set_value(Settings::SECTION, Settings::FILE_KEY, p_not_needed ? Variant(true) : Variant());
	} else {
		PackedStringArray meshes = config->get_value(Settings::SECTION, Settings::MESHES_KEY, PackedStringArray());
		const int mesh_index = meshes.find(p_mesh);
		if (p_not_needed && mesh_index < 0) {
			meshes.push_back(p_mesh);
			meshes.sort();
		} else if (!p_not_needed && mesh_index >= 0) {
			meshes.remove_at(mesh_index);
		}
		config->set_value(Settings::SECTION, Settings::MESHES_KEY, meshes.is_empty() ? Variant() : Variant(meshes));
	}
	error = config->save(path);
	ERR_FAIL_COND_V(error != OK, error);

	// Changing an editor annotation must not reimport the model. Do not acknowledge
	// pre-existing import changes: those still need their normal reimport.
	EditorFileSystemDirectory::FileInfo *file = directory->files[index];
	if (file->import_md5 == previous_md5) {
		file->import_md5 = FileAccess::get_md5(path);
		file->import_modified_time = FileAccess::get_modified_time(path);
		_save_filesystem_cache();
	}
	return OK;
}
