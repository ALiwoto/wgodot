// wgodot-changes::file
#include "wgodot_cpp_cache.h"
#include "wgodot_cpp_project.h"

#include "wgodot_resource_rewrite.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/io/wgodot_build_fingerprint.h"
#include "core/io/wgodot_file_stamp.h"
#include "core/os/os.h"
#include "core/object/worker_thread_pool.h"
#include "core/object/script_language.h"

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
	return WGodotFileStamp::get(p_path);
}

Dictionary file_stamp(const String &p_path, const String &p_digest) {
	Dictionary stamp;
	stamp["digest"] = p_digest;
	stamp["revision"] = WGodotFileStamp::get(p_path);
	return stamp;
}

void resource_dependencies(const String &p_path, PackedStringArray &r_dependencies) {
	List<String> paths;
	ResourceLoader::get_dependencies(p_path, &paths);
	for (const String &dependency : paths) {
		String resolved = dependency.get_slice("::", 0);
		if (resolved.begins_with("uid://")) {
			r_dependencies.push_back(resolved);
			resolved = dependency.get_slice("::", 2);
		}
		if (!resolved.is_empty()) {
			r_dependencies.push_back(resolved.is_relative_path() ? p_path.get_base_dir().path_join(resolved).simplify_path() : resolved);
		}
	}
	r_dependencies.sort();
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

Error WGodotCppCache::open(const String &p_directory, const String &p_target, bool p_trace, int p_jobs) {
	directory = p_directory;
	jobs = p_jobs;
	// Rebuilding the compiler invalidates its disposable cache without reading
	// the executable or every project resource into a hashing function.
	const String &compiler = WGodotBuildFingerprint::get();
	ERR_FAIL_COND_V_MSG(compiler.is_empty(), ERR_CANT_OPEN, "Cannot fingerprint the running gd2cpp compiler.");
	identity = ("metadata-v1\n" + compiler + "\n" + p_target + "\n" + itos(p_trace) + "\n" + itos(bool(GLOBAL_GET("wgodot/gdscript/enforce_resource_paths"))) + "\n" +
			ProjectSettings::get_singleton()->get_resource_path() + "\n" + digest("res://project.godot") + "\n" +
			digest("res://override.cfg") + "\n" + digest("res://wgodot_targets.cfg") + "\n" + digest("res://export_presets.cfg"))
					   .sha256_text();
	const String path = directory.path_join(CACHE_FILE);
	uint64_t phase_started = OS::get_singleton()->get_ticks_msec();
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
	old_inputs = previous.get("inputs", Dictionary());
	old_units = previous.get("units", Dictionary());
	old_outputs = previous.get("outputs", Dictionary());
	print_line(vformat("Native cache load: %d ms", OS::get_singleton()->get_ticks_msec() - phase_started));
	phase_started = OS::get_singleton()->get_ticks_msec();
	RETURN_IF_ERROR(scan("res://"));
	inventory.sort();
	print_line(vformat("Native cache inventory: %d files, %d ms", inventory.size(), OS::get_singleton()->get_ticks_msec() - phase_started));
	RETURN_IF_ERROR(WGodotCppProject::refresh_script_classes(inventory));
	LocalVector<StringName> names;
	ScriptServer::get_global_class_list(names);
	for (const StringName &name : names) {
		named_types[String(name)] = ScriptServer::get_global_class_path(name) + "\n" + String(ScriptServer::get_global_class_base(name));
	}
	types_changed = named_types != Dictionary(previous.get("named_types", Dictionary()));
	return OK;
}

void WGodotCppCache::prepare_dependencies() {
	struct Input {
		String path;
		PackedStringArray dependencies;
	};
	struct Batch {
		Vector<Input> items;
		Input *data = nullptr;
		void inspect(uint32_t p_index, void *) {
			Input &item = data[p_index];
			resource_dependencies(item.path, item.dependencies);
		}
	} batch;
	for (const String &path : inventory) {
		const String extension = path.get_extension();
		if (extension != "tres" && extension != "tscn") {
			continue;
		}
		const Dictionary previous_input = old_inputs.get(path, Dictionary());
		if (types_changed || String(previous_input.get("digest", "")) != digest(path)) {
			batch.items.push_back({ path, PackedStringArray() });
		}
	}
	if (batch.items.is_empty()) {
		return;
	}
	print_line(vformat("Inspecting dependencies: %d changed resources (%d workers)", batch.items.size(), jobs));
	const uint64_t started = OS::get_singleton()->get_ticks_msec();
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	batch.data = batch.items.ptrw();
	const auto task = pool->add_template_group_task(&batch, &Batch::inspect, static_cast<void *>(nullptr), batch.items.size(), jobs, true, "Inspect resource dependencies");
	pool->wait_for_group_task_completion(task);
	for (const Input &input : batch.items) {
		prepared_dependencies.insert(input.path, input.dependencies);
	}
	print_line(vformat("Native dependency discovery: %d ms", OS::get_singleton()->get_ticks_msec() - started));
}

String WGodotCppCache::digest(const String &p_path) {
	if (const String *cached = digests.getptr(p_path)) {
		return *cached;
	}
	// Metadata is sampled once, then checked again at publication. Explicitly
	// remove the cache when deliberately restoring both bytes and timestamps.
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
	Dictionary entry = old_inputs.get(path, Dictionary());
	const String current = digest(path);
	PackedStringArray dependencies = entry.get("dependencies", PackedStringArray());
	if (String(entry.get("digest", "")) != current || prepared_dependencies.has(path)) {
		dependencies.clear();
		const String ext = path.get_extension();
		const bool uid = path.begins_with("uid://");
		if (uid && current != "<unresolved-uid>") {
			dependencies.push_back(current);
		}
		if (const auto *prepared = prepared_dependencies.getptr(path)) {
			dependencies = *prepared;
		} else if (!uid && current != "<missing>" && ext != "gd" && ext != "import" && ResourceLoader::get_resource_type(path) != String()) {
			resource_dependencies(path, dependencies);
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
	dependencies_dirty = true;
	for (const String &dependency : dependencies) {
		track(dependency);
	}
}

void WGodotCppCache::set_dependencies(const String &p_path, const PackedStringArray &p_dependencies) {
	track(p_path);
	Dictionary entry = inputs[p_path];
	PackedStringArray dependencies = p_dependencies;
	dependencies.sort();
	entry["dependencies"] = dependencies;
	dependencies_dirty = true;
	for (const String &dependency : p_dependencies) {
		track(dependency);
	}
}

void WGodotCppCache::set_resource_signature(const String &p_path, const String &p_signature) {
	track(p_path);
	Dictionary entry = inputs[p_path];
	entry["resource_signature"] = p_signature;
	dependencies_dirty = true;
}

void WGodotCppCache::update_changes() {
	if (!dependencies_dirty) {
		return;
	}
	changed.clear();
	changed_resources.clear();
	HashMap<String, Vector<String>> consumers;
	for (const KeyValue<Variant, Variant> &input : inputs) {
		const String path = input.key;
		const Dictionary current = input.value;
		const Dictionary old = old_inputs.get(path, Dictionary());
		const bool dependencies_changed = current["dependencies"] != old.get("dependencies", PackedStringArray());
		if (dependencies_changed || current["digest"] != old.get("digest", "")) {
			changed.insert(path);
		}
		if (dependencies_changed || current.get("resource_signature", current["digest"]) != old.get("resource_signature", old.get("digest", ""))) {
			changed_resources.insert(path);
		}
		for (const String &dependency : PackedStringArray(current["dependencies"])) {
			consumers[dependency].push_back(path);
		}
	}
	// Propagate once for the entire graph, including cycles. Walking every
	// resource's transitive dependencies independently makes catalogs quadratic.
	auto propagate = [&](HashSet<String> &p_changed) {
		Vector<String> pending;
		for (const String &path : p_changed) {
			pending.push_back(path);
		}
		for (int index = 0; index < pending.size(); ++index) {
			const auto *parents = consumers.getptr(pending[index]);
			if (!parents) {
				continue;
			}
			for (const String &parent : *parents) {
				if (!p_changed.has(parent)) {
					p_changed.insert(parent);
					pending.push_back(parent);
				}
			}
		}
	};
	propagate(changed);
	propagate(changed_resources);
	dependencies_dirty = false;
}

bool WGodotCppCache::output_matches(const String &p_name, const String &p_digest) {
	const String key = p_name + ":" + p_digest;
	if (const bool *valid = valid_outputs.getptr(key)) {
		return *valid;
	}
	const String path = directory.path_join(p_name);
	const Dictionary old = old_outputs.get(p_name, Dictionary());
	bool valid = false;
	if (FileAccess::exists(path)) {
		valid = String(old.get("digest", "")) == p_digest && String(old.get("revision", "")) == WGodotFileStamp::get(path);
	}
	valid_outputs.insert(key, valid);
	return valid;
}

bool WGodotCppCache::up_to_date() {
	prepare_dependencies();
	for (const String &path : inventory) {
		track(path);
	}
	if (!compatible || PackedStringArray(previous.get("inventory", PackedStringArray())) != inventory) {
		return false;
	}
	update_changes();
	if (!changed.is_empty()) {
		return false;
	}
	if (old_outputs.is_empty()) {
		return false;
	}
	for (const KeyValue<Variant, Variant> &entry : old_outputs) {
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
	const Dictionary entry = old_units.get(p_key, Dictionary());
	if (entry.is_empty() || String(entry["context"]) != p_context) {
		return Dictionary();
	}
	const PackedStringArray dependencies = entry["inputs"];
	for (const String &path : dependencies) {
		track(path);
	}
	update_changes();
	const auto &invalid = p_key.begins_with("class:") ? changed : changed_resources;
	for (const String &path : dependencies) {
		if (invalid.has(path)) {
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
	state["named_types"] = named_types;
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
