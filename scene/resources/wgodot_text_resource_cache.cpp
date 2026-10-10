// wgodot-changes::file
#include "wgodot_text_resource_cache.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_format_binary.h"
#include "core/io/wgodot_resource_serialization.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/version.h"

WGodotTextResourceCache::WGodotTextResourceCache(const String &p_source_path) {
#ifdef TOOLS_ENABLED
	if (!p_source_path.begins_with("res://") || p_source_path.get_extension() != "tres" || WGodotResourceSerialization::is_active()) {
		return;
	}
	const String data_path = ProjectSettings::get_singleton()->get_project_data_path();
	if (p_source_path.begins_with(data_path + "/")) {
		return;
	}
	source_path = p_source_path;
	// Content identity also detects same-size edits within one filesystem timestamp
	// tick and restored/older timestamps. Unchanged sources are never reserialized.
	source_hash = FileAccess::get_md5(source_path);
	if (source_hash.is_empty()) {
		return;
	}
	// Native defaults/serialization can change between local fork builds even
	// when their Godot version number and Git commit have not changed.
	static const String version = String(GODOT_VERSION_FULL_CONFIG) + "-v1-" + uitos(FileAccess::get_modified_time(OS::get_singleton()->get_executable_path()));
	cache_path = data_path.path_join("resource_cache").path_join(version).path_join(source_path.get_file() + "-" + source_path.md5_text() + "-" + source_hash + ".res");
#endif
}

Ref<Resource> WGodotTextResourceCache::load(const String &p_original_path, bool p_use_sub_threads, float *r_progress, ResourceFormatLoader::CacheMode p_cache_mode) const {
	if (cache_path.is_empty() || !FileAccess::exists(cache_path)) {
		return Ref<Resource>();
	}
	ResourceFormatLoaderBinary loader;
	Error error = OK;
	// All resource and subresource identities remain anchored to the text source.
	return loader.load(cache_path, p_original_path, &error, p_use_sub_threads, r_progress, p_cache_mode);
}

void WGodotTextResourceCache::store(const Ref<Resource> &p_resource) const {
	if (cache_path.is_empty() || FileAccess::get_md5(source_path) != source_hash) {
		return;
	}
	if (DirAccess::make_dir_recursive_absolute(cache_path.get_base_dir()) != OK) {
		return; // A read-only project must still load its source normally.
	}
	const String temporary = cache_path + "." + itos(OS::get_singleton()->get_process_id()) + "." + uitos(Thread::get_caller_id()) + ".tmp";
	ResourceFormatSaverBinaryInstance saver;
	if (saver.save(temporary, p_resource, ResourceSaver::FLAG_COMPRESS) != OK) {
		return;
	}
	// Publish only complete payloads. Concurrent editor/game readers never see a
	// partially written cache, and interrupted writes cannot replace the source.
	DirAccess::rename_absolute(temporary, cache_path);
}
