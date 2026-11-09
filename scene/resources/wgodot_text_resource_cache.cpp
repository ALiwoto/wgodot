// wgodot-changes::file
#include "wgodot_text_resource_cache.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/resource_format_binary.h"
#include "core/io/wgodot_build_fingerprint.h"
#include "core/io/wgodot_file_stamp.h"
#include "core/io/wgodot_resource_serialization.h"
#include "core/io/wgodot_resource_trace.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/os/thread.h"

namespace {
constexpr uint32_t CACHE_TRAILER = 0x57474331; // WGC1, after the ordinary binary resource.

void collect_scripts(const Variant &p_value, Dictionary &r_scripts) {
	if (p_value.get_type() == Variant::OBJECT) {
		const Ref<Resource> resource = p_value;
		if (resource.is_null()) {
			return;
		}
		Ref<Script> script = resource;
		if (script.is_null()) {
			script = resource->get_script();
		}
		while (script.is_valid() && !r_scripts.has(script->get_path())) {
			PackedStringArray identity;
			identity.push_back(WGodotFileStamp::get(script->get_path()));
			identity.push_back(script->get_global_name());
			r_scripts[script->get_path()] = identity;
			script = script->get_base_script();
		}
	} else if (p_value.get_type() == Variant::ARRAY) {
		const Array values = p_value;
		collect_scripts(values.get_typed_script(), r_scripts);
		for (const Variant &value : values) {
			collect_scripts(value, r_scripts);
		}
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary values = p_value;
		collect_scripts(values.get_typed_key_script(), r_scripts);
		collect_scripts(values.get_typed_value_script(), r_scripts);
		for (const KeyValue<Variant, Variant> &entry : values) {
			collect_scripts(entry.key, r_scripts);
			collect_scripts(entry.value, r_scripts);
		}
	}
}

bool scripts_match(const Dictionary &p_scripts) {
	for (const KeyValue<Variant, Variant> &entry : p_scripts) {
		const String path = entry.key;
		const PackedStringArray identity = entry.value;
		if (identity.size() != 2 || identity[0] == "<missing>" || WGodotFileStamp::get(path) != identity[0]) {
			return false;
		}
		const StringName name = identity[1];
		if (name != StringName() && (!ScriptServer::is_global_class(name) || ScriptServer::get_global_class_path(name) != path)) {
			return false;
		}
	}
	return true;
}
} // namespace

WGodotTextResourceCache::WGodotTextResourceCache(const String &p_source_path) {
#ifdef TOOLS_ENABLED
	const String extension = p_source_path.get_extension();
	if (!p_source_path.begins_with("res://") || (extension != "tres" && extension != "tscn") || WGodotResourceSerialization::is_active()) {
		return;
	}
	const String data_path = ProjectSettings::get_singleton()->get_project_data_path();
	if (p_source_path.begins_with(data_path + "/")) {
		return;
	}
	source_path = p_source_path;
	WGodotResourceTrace trace("cache.verify_source", source_path);
	source_hash = WGodotFileStamp::get(source_path);
	if (source_hash == "<missing>") {
		return;
	}
	// Loader, codec, scene-type and build-option changes invalidate automatically.
	const String &version = WGodotBuildFingerprint::get();
	if (version.is_empty()) {
		return; // Without a build identity, load the authored text without caching.
	}
	cache_path = data_path.path_join("resource_cache").path_join(version).path_join(source_path.get_file() + "-" + source_path.md5_text() + "-" + source_hash + ".res");
#endif
}

Ref<Resource> WGodotTextResourceCache::load(const String &p_original_path, bool p_use_sub_threads, float *r_progress, ResourceFormatLoader::CacheMode p_cache_mode) const {
	if (cache_path.is_empty()) {
		return Ref<Resource>();
	}
	// Do not check existence and then reopen through the binary loader. Another
	// process can publish/remove this disposable cache between those operations.
	Ref<FileAccess> file = FileAccess::open(cache_path, FileAccess::READ);
	if (file.is_null()) {
		return Ref<Resource>();
	}
	WGodotResourceTrace trace("cache.load", source_path);
	// Validate the small script table before creating any resources. Script defaults,
	// setters, base classes and named-type bindings must come from the current source.
	const uint64_t length = file->get_length();
	if (length < 12) {
		return Ref<Resource>();
	}
	file->seek(length - 12);
	const uint64_t metadata_offset = file->get_64();
	if (file->get_32() != CACHE_TRAILER || metadata_offset >= length - 12) {
		return Ref<Resource>();
	}
	file->seek(metadata_offset);
	const Variant metadata = file->get_var(false);
	if (metadata.get_type() != Variant::DICTIONARY || !scripts_match(metadata)) {
		return Ref<Resource>();
	}
	file->seek(0);
	ResourceFormatLoaderBinary loader;
	Error error = OK;
	// All resource and subresource identities remain anchored to the text source.
	return loader.load_from_file(file, p_original_path, &error, p_use_sub_threads, r_progress, p_cache_mode);
}

void WGodotTextResourceCache::store(const Ref<Resource> &p_resource, const WGodotResourceProperties &p_source_properties, const WGodotResourceUIDs &p_source_uids) const {
	WGodotResourceTrace trace("cache.store", source_path);
	if (cache_path.is_empty() || WGodotFileStamp::get(source_path) != source_hash) {
		return;
	}
	if (DirAccess::make_dir_recursive_absolute(cache_path.get_base_dir()) != OK) {
		return; // A read-only project must still load its source normally.
	}
	Dictionary scripts;
	for (const auto &entry : p_source_properties) {
		collect_scripts(entry.key, scripts);
		for (const WGodotResourceProperty &property : entry.value) {
			collect_scripts(property.value, scripts);
		}
	}
	if (!scripts_match(scripts)) {
		return;
	}
	const String temporary = cache_path + "." + itos(OS::get_singleton()->get_process_id()) + "." + uitos(Thread::get_caller_id()) + ".tmp";
	ResourceFormatSaverBinaryInstance saver;
	if (saver.save(temporary, p_resource, ResourceSaver::FLAG_COMPRESS, &p_source_properties, &p_source_uids) != OK) {
		if (FileAccess::exists(temporary)) {
			DirAccess::remove_absolute(temporary);
		}
		return;
	}
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::READ_WRITE);
	if (file.is_null()) {
		DirAccess::remove_absolute(temporary);
		return;
	}
	file->seek_end();
	const uint64_t metadata_offset = file->get_position();
	file->store_var(scripts);
	file->store_64(metadata_offset);
	file->store_32(CACHE_TRAILER);
	file->flush();
	const bool valid = file->get_error() == OK && scripts_match(scripts) && WGodotFileStamp::get(source_path) == source_hash;
	file.unref();
	if (!valid) {
		DirAccess::remove_absolute(temporary);
		return;
	}
	// Publish only complete payloads. Concurrent editor/game readers never see a
	// partially written cache, and interrupted writes cannot replace the source.
	if (DirAccess::rename_absolute(temporary, cache_path) != OK) {
		DirAccess::remove_absolute(temporary);
	}
}
