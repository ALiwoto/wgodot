// wgodot-changes::file
#include "wgodot_cpp_cache.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/io/wgodot_build_fingerprint.h"
#include "wgodot_resource_rewrite.h"

namespace {
constexpr const char *CACHE_FILE = ".gd2cpp-cache.bin";

String input_digest(const String &p_path) {
	if (p_path.begins_with("uid://")) {
		ResourceUID *uids = ResourceUID::get_singleton();
		const ResourceUID::ID id = uids->text_to_id(p_path);
		// Import settings can retain UIDs for save destinations that no longer
		// exist. Record the mapping itself, including its unresolved state, so a
		// later resolution/remap invalidates consumers without logging a load error.
		return uids->has_id(id) ? uids->get_id_path(id) : String("<unresolved-uid>");
	}
	return FileAccess::exists(p_path) ? FileAccess::get_sha256(p_path) : String("<missing>");
}

Dictionary file_stamp(const String &p_path, const String &p_digest) {
	Dictionary stamp;
	stamp["digest"] = p_digest;
	stamp["size"] = FileAccess::get_size(p_path);
	stamp["modified"] = int64_t(FileAccess::get_modified_time(p_path));
	return stamp;
}
} // namespace

Error WGodotCppCache::scan(const String &p_directory) {
	Error error;
	Ref<DirAccess> dir = DirAccess::open(p_directory, &error);
	RETURN_IF_ERROR(error);
	if (dir->file_exists(".gdignore")) {
		inventory.push_back(p_directory.path_join(".gdignore"));
		return OK;
	}
	dir->list_dir_begin();
	for (String name = dir->get_next(); !name.is_empty(); name = dir->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		const String path = p_directory.path_join(name);
		if (dir->current_is_dir()) {
			ERR_FAIL_COND_V_MSG(dir->is_link(name), ERR_UNAVAILABLE, "Native export does not support directory symlinks: " + path);
			RETURN_IF_ERROR(scan(path));
		} else {
			// Inventory detects additions/removals as well as content changes.
			// Binary dependencies are tracked on demand through resource imports.
			const String ext = name.get_extension();
			if (ext == "gd" || ext == "tscn" || ext == "tres" || ext == "scn" || ext == "res" || ext == "gdshader" || ext == "gdshaderinc" || ext == "glsl" || ext == "import" || ext == "cfg" || ext == "godot" || ext == "gdextension" || ext == "uid") {
				inventory.push_back(path);
			}
		}
	}
	return OK;
}

Error WGodotCppCache::open(const String &p_directory, const String &p_target, bool p_trace) {
	directory = p_directory;
	// Fingerprint the running compiler, including parsers, codecs, native API
	// metadata and build options. No manual version bump or source checkout needed.
	const String &compiler = WGodotBuildFingerprint::get();
	ERR_FAIL_COND_V_MSG(compiler.is_empty(), ERR_CANT_OPEN, "Cannot fingerprint the running gd2cpp compiler.");
	identity = (compiler + "\n" + p_target + "\n" + itos(p_trace) + "\n" +
			ProjectSettings::get_singleton()->get_resource_path() + "\n" + digest("res://project.godot") + "\n" +
			digest("res://override.cfg") + "\n" + digest("res://wgodot_targets.cfg") + "\n" + digest("res://export_presets.cfg")).sha256_text();
	const String path = directory.path_join(CACHE_FILE);
	if (FileAccess::exists(path)) {
		const Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
		if (file.is_valid()) {
			const Variant data = file->get_var(false);
			if (data.get_type() == Variant::DICTIONARY) {
				previous = data;
				compatible = String(previous.get("identity", "")) == identity;
			}
		}
	}
	RETURN_IF_ERROR(scan("res://"));
	inventory.sort();
	return OK;
}

String WGodotCppCache::digest(const String &p_path) {
	if (const String *cached = digests.getptr(p_path)) {
		return *cached;
	}
	// Source content is always checked, including same-size/same-timestamp edits.
	const String result = input_digest(p_path);
	digests.insert(p_path, result);
	return result;
}

void WGodotCppCache::track(const String &p_path) {
	const String path = p_path.get_slice("::", 0);
	if (inspected.has(path)) {
		return;
	}
	inspected.insert(path);
	const Dictionary old_inputs = previous.get("inputs", Dictionary());
	Dictionary entry = old_inputs.get(path, Dictionary());
	const String current = digest(path);
	PackedStringArray dependencies = entry.get("dependencies", PackedStringArray());
	if (!compatible || String(entry.get("digest", "")) != current) {
		dependencies.clear();
		const String ext = path.get_extension();
		const bool uid = path.begins_with("uid://");
		if (uid && current != "<unresolved-uid>") {
			dependencies.push_back(current);
		}
		if (!uid && current != "<missing>" && ext != "gd" && ext != "import" && ResourceLoader::get_resource_type(path) != String()) {
			List<String> paths;
			ResourceLoader::get_dependencies(path, &paths);
			for (const String &dependency : paths) {
				String resolved = dependency.get_slice("::", 0);
				if (resolved.begins_with("uid://")) {
					dependencies.push_back(resolved);
					// ResourceLoader supplies uid::type::fallback_path (the type may
					// be empty). Keep the fallback as well as the UID mapping.
					resolved = dependency.get_slice("::", 2);
				}
				if (!resolved.is_empty()) {
					dependencies.push_back(resolved.is_relative_path() ? path.get_base_dir().path_join(resolved).simplify_path() : resolved);
				}
			}
		}
		if (!uid && current != "<missing>" && ext == "import") {
			Ref<ConfigFile> config;
			config.instantiate();
			const Error error = config->load(path);
			if (error == OK) {
				// Track cooked payloads, import scripts and source-side dependencies.
				for (const String &section : config->get_sections()) {
					for (const String &key : config->get_section_keys(section)) {
						WGodotResourceRewrite::value(config->get_value(section, key), PropertyInfo(), [&](const String &p_dependency) {
							dependencies.push_back(p_dependency);
							return p_dependency;
						});
					}
				}
			} else {
				ERR_PRINT("Cannot read native export dependency settings " + path + ": " + error_names[error]);
			}
		}
		if (!uid && FileAccess::exists(path + ".import")) {
			dependencies.push_back(path + ".import");
		}
	}
	entry = Dictionary();
	entry["digest"] = current;
	entry["dependencies"] = dependencies;
	inputs[path] = entry;
	for (const String &dependency : dependencies) {
		track(dependency);
	}
}

void WGodotCppCache::set_dependencies(const String &p_path, const PackedStringArray &p_dependencies) {
	track(p_path);
	Dictionary entry = inputs[p_path];
	entry["dependencies"] = p_dependencies;
	for (const String &dependency : p_dependencies) {
		track(dependency);
	}
}

bool WGodotCppCache::unchanged(const String &p_path, HashSet<String> &r_seen) {
	const String path = p_path.get_slice("::", 0);
	if (r_seen.has(path)) {
		return true;
	}
	r_seen.insert(path);
	track(path);
	const Dictionary old_inputs = previous.get("inputs", Dictionary());
	const Dictionary old = old_inputs.get(path, Dictionary());
	const Dictionary current = inputs[path];
	if (String(current["digest"]).is_empty() || String(old.get("digest", "")) != String(current["digest"])) {
		return false;
	}
	const PackedStringArray dependencies = current["dependencies"];
	for (const String &dependency : dependencies) {
		if (!unchanged(dependency, r_seen)) {
			return false;
		}
	}
	return true;
}

bool WGodotCppCache::output_matches(const String &p_name, const String &p_digest) {
	const String key = p_name + ":" + p_digest;
	if (const bool *valid = valid_outputs.getptr(key)) {
		return *valid;
	}
	const String path = directory.path_join(p_name);
	const Dictionary outputs = previous.get("outputs", Dictionary());
	const Dictionary old = outputs.get(p_name, Dictionary());
	bool valid = false;
	if (FileAccess::exists(path)) {
		valid = String(old.get("digest", "")) == p_digest && int64_t(old.get("size", -1)) == FileAccess::get_size(path) &&
				int64_t(old.get("modified", -1)) == int64_t(FileAccess::get_modified_time(path));
		if (!valid) {
			valid = FileAccess::get_sha256(path) == p_digest;
		}
	}
	valid_outputs.insert(key, valid);
	return valid;
}

bool WGodotCppCache::up_to_date() {
	if (!compatible || PackedStringArray(previous.get("inventory", PackedStringArray())) != inventory) {
		return false;
	}
	HashSet<String> seen;
	for (const String &path : inventory) {
		if (!unchanged(path, seen)) {
			return false;
		}
	}
	const Dictionary old_inputs = previous.get("inputs", Dictionary());
	for (const KeyValue<Variant, Variant> &entry : old_inputs) {
		if (!unchanged(entry.key, seen)) {
			return false;
		}
	}
	const Dictionary outputs = previous.get("outputs", Dictionary());
	if (outputs.is_empty()) {
		return false;
	}
	for (const KeyValue<Variant, Variant> &entry : outputs) {
		const Dictionary stamp = entry.value;
		if (!output_matches(entry.key, stamp["digest"])) {
			return false;
		}
	}
	return true;
}

Dictionary WGodotCppCache::reuse(const String &p_key, const String &p_context) {
	if (!compatible) {
		return Dictionary();
	}
	const Dictionary old_units = previous.get("units", Dictionary());
	const Dictionary entry = old_units.get(p_key, Dictionary());
	if (entry.is_empty() || String(entry["context"]) != p_context) {
		return Dictionary();
	}
	HashSet<String> seen;
	const PackedStringArray dependencies = entry["inputs"];
	for (const String &path : dependencies) {
		if (!unchanged(path, seen)) {
			return Dictionary();
		}
	}
	const Dictionary payload = entry["payload"];
	const Dictionary files = payload["files"];
	for (const KeyValue<Variant, Variant> &file : files) {
		if (!output_matches(file.key, file.value)) {
			return Dictionary();
		}
	}
	units[p_key] = entry;
	return payload;
}

void WGodotCppCache::record(const String &p_key, const String &p_context, const PackedStringArray &p_inputs, const Dictionary &p_payload) {
	for (const String &path : p_inputs) {
		track(path);
	}
	Dictionary entry;
	entry["context"] = p_context;
	entry["inputs"] = p_inputs;
	entry["payload"] = p_payload;
	units[p_key] = entry;
}

Error WGodotCppCache::commit(const HashMap<String, String> &p_files, int p_class_count) {
	for (const String &path : inventory) {
		track(path);
	}
	// Never bless mixed revisions if the project was edited during a long export.
	for (const KeyValue<Variant, Variant> &input : inputs) {
		const String path = input.key;
		const Dictionary entry = input.value;
		const String actual = input_digest(path);
		if (actual.is_empty() || actual != String(entry["digest"])) {
			DirAccess::remove_absolute(directory.path_join("main_game.json"));
			ERR_FAIL_V_MSG(ERR_BUSY, "Native export input changed during generation; run export again: " + path);
		}
	}
	Dictionary outputs;
	for (const auto &file : p_files) {
		outputs[file.key] = file_stamp(directory.path_join(file.key), file.value);
	}
	const String manifest = directory.path_join("main_game.json");
	outputs["main_game.json"] = file_stamp(manifest, FileAccess::get_sha256(manifest));
	Dictionary state;
	state["identity"] = identity;
	state["inventory"] = inventory;
	state["inputs"] = inputs;
	state["units"] = units;
	state["outputs"] = outputs;
	state["class_count"] = p_class_count;
	const String temporary = directory.path_join(String(CACHE_FILE) + ".tmp");
	Error error;
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE, &error);
	RETURN_IF_ERROR(error);
	file->store_var(state);
	file->flush();
	RETURN_IF_ERROR(file->get_error());
	file.unref();
	return DirAccess::rename_absolute(temporary, directory.path_join(CACHE_FILE));
}
