// wgodot-changes::file
#include "wgodot_resource_check.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/wgodot_build_fingerprint.h"
#include "core/io/wgodot_file_stamp.h"
#include "core/io/wgodot_global_resources.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "editor/file_system/editor_file_system.h"

HashMap<String, WGodotResourceCheck::Entry> WGodotResourceCheck::cache;
String WGodotResourceCheck::context;

namespace {

void collect_resources(EditorFileSystemDirectory *p_directory, Vector<String> &r_paths) {
	for (int index = 0; index < p_directory->get_file_count(); index++) {
		const String extension = p_directory->get_file(index).get_extension();
		if (extension == "tscn" || extension == "tres") {
			r_paths.push_back(p_directory->get_file_path(index));
		}
	}
	for (int index = 0; index < p_directory->get_subdir_count(); index++) {
		collect_resources(p_directory->get_subdir(index), r_paths);
	}
}

String get_class_identity() {
	LocalVector<StringName> classes;
	Vector<String> declarations;
	// Built-in types are covered by the engine fingerprint. Internal editor
	// classes register lazily, so the entire runtime ClassDB is not a stable key.
	ClassDB::get_extensions_class_list(classes);
	for (const StringName &name : classes) {
		declarations.push_back(name);
	}
	// Script declarations, including their base classes, are checked per resource.
	// Adding an unrelated global script class must not discard every cached result.
	for (const auto &entry : WGlobalResources::paths()) {
		declarations.push_back(String(entry.key) + "=" + entry.value);
	}
	declarations.sort();
	return String("\n").join(declarations).md5_text();
}

} // namespace

void WGodotResourceCheck::invalidate(const HashSet<String> &p_paths, bool p_all) {
	if (p_all) {
		cache.clear();
		return;
	}
	for (const String &path : p_paths) {
		const String extension = path.get_extension();
		if (extension == "tscn" || extension == "tres") {
			cache.erase(path);
		} else if (extension.is_empty()) {
			// Directory moves/deletions can affect every descendant without
			// generating a separate notification for each resource.
			cache.clear();
		}
	}
}

void WGodotResourceCheck::start(const WGodotGDScriptExportTransform::ExportProject &p_project) {
	Vector<String> paths;
	collect_resources(EditorFileSystem::get_singleton()->get_filesystem(), paths);
	start(paths, p_project);
}

void WGodotResourceCheck::start(const Vector<String> &p_paths, const WGodotGDScriptExportTransform::ExportProject &p_project) {
	project = p_project;
	start_paths(p_paths);
}

void WGodotResourceCheck::start(const Vector<String> &p_paths, const WGodotResourceTypes &p_types) {
	types = std::make_unique<const WGodotResourceTypes>(p_types);
	start_paths(p_paths);
}

void WGodotResourceCheck::start_paths(const Vector<String> &p_paths) {
	class_identity = get_class_identity();
	const String &build = WGodotBuildFingerprint::get();
	if (!build.is_empty()) {
		// Use the same engine identity and disposable directory as binary resources.
		cache_path = ProjectSettings::get_singleton()->get_project_data_path().path_join("resource_cache").path_join(build).path_join("typed.bin");
	}
	cache_context = ProjectSettings::get_singleton()->get_resource_path() + "\n" + build + "\n" + class_identity;
	if (context != cache_context) {
		context = cache_context;
		cache.clear();
		const Ref<FileAccess> file = cache_path.is_empty() ? Ref<FileAccess>() : FileAccess::open(cache_path, FileAccess::READ);
		if (file.is_valid()) {
			const Variant data = file->get_var(false);
			if (data.get_type() == Variant::DICTIONARY) {
				const Dictionary saved = data;
				if (String(saved.get("classes", "")) == class_identity) {
					const Dictionary entries = saved.get("entries", Dictionary());
					for (const Variant &key : entries.get_key_list()) {
						const Dictionary value = entries[key];
						Entry entry;
						entry.digest = value.get("digest", "");
						entry.syntax.error = Error(int(value.get("error", OK)));
						entry.syntax.line = value.get("line", 1);
						entry.syntax.message = value.get("message", "");
						entry.syntax.missing_newlines = value.get("missing_newlines", PackedInt32Array());
						const Dictionary dependencies = value.get("dependencies", Dictionary());
						for (const KeyValue<Variant, Variant> &dependency : dependencies) {
							entry.syntax.dependencies.insert(dependency.key, dependency.value);
						}
						const Dictionary declarations = value.get("declarations", Dictionary());
						for (const KeyValue<Variant, Variant> &declaration : declarations) {
							entry.syntax.declarations.insert(declaration.key, declaration.value);
						}
						cache.insert(key, entry);
					}
				}
			}
		}
	}
	Vector<String> paths = p_paths;
	paths.sort();
	inventory_changed = paths.size() != cache.size();
	for (const String &path : paths) {
		Item item;
		item.path = path;
		if (const Entry *entry = cache.getptr(path)) {
			item.entry = *entry;
		}
		items.push_back(item);
	}
	task = WorkerThreadPool::get_singleton()->add_native_task(&WGodotResourceCheck::run, this, false, "Check resource syntax");
}

void WGodotResourceCheck::check_item(uint32_t p_index, Item **p_pending) {
	if (cancelled.is_set()) {
		return;
	}
	Item &item = *p_pending[p_index];
	Entry &entry = item.entry;
	const String digest = WGodotFileStamp::get(item.path);
	bool reusable = !digest.is_empty() && digest == entry.digest && entry.syntax.error == OK;
	for (const auto &dependency : entry.syntax.dependencies) {
		reusable &= WGodotFileStamp::get(dependency.key) == dependency.value;
	}
	for (const auto &declaration : entry.syntax.declarations) {
		reusable &= types->signature(declaration.key) == declaration.value;
	}
	if (reusable) {
		return;
	}
	item.changed = true;
	entry.digest = digest;
	entry.syntax = WGodotResourceSyntax::Result();
	Error error = OK;
	const Ref<FileAccess> file = FileAccess::open(item.path, FileAccess::READ, &error);
	if (file.is_null()) {
		entry.digest = String();
		entry.syntax.error = error;
		entry.syntax.message = "Cannot read resource: " + String(error_names[error]);
		return;
	}
	item.parsed = true;
	entry.syntax = WGodotResourceSyntax::check(file, types.get());
	bool stable = WGodotFileStamp::get(item.path) == digest;
	for (const auto &dependency : entry.syntax.dependencies) {
		stable &= WGodotFileStamp::get(dependency.key) == dependency.value;
	}
	if (!stable) {
		entry.digest = String();
		entry.syntax.error = ERR_BUSY;
		entry.syntax.message = "Resource changed during validation; run check again.";
	}
}

void WGodotResourceCheck::run(void *p_userdata) {
	auto *check = static_cast<WGodotResourceCheck *>(p_userdata);
	const uint64_t started = OS::get_singleton()->get_ticks_msec();
	String declaration_error;
	Error declaration_result = OK;
	if (!check->types) {
		auto declarations = std::make_unique<WGodotResourceTypes>();
		declaration_result = declarations->build(check->project, declaration_error);
		check->types = std::move(declarations);
	}
	Vector<Item *> pending;
	for (Item &item : check->items) {
		if (declaration_result == OK) {
			pending.push_back(&item);
		}
	}
	if (!pending.is_empty()) {
		WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
		const WorkerThreadPool::GroupID group = pool->add_template_group_task(
				check, &WGodotResourceCheck::check_item, pending.ptrw(), pending.size(), -1, false, "Parse resource syntax");
		pool->wait_for_group_task_completion(group);
	}
	if (check->cancelled.is_set()) {
		return;
	}
	int parsed = 0;
	int cached = 0;
	int errors = 0;
	int warnings = 0;
	bool changed = check->inventory_changed;
	PackedStringArray output;
	Array diagnostics;
	if (declaration_result != OK) {
		errors++;
		output.push_back("wgodot-check: error: " + declaration_error);
		Dictionary diagnostic;
		diagnostic["path"] = "res://";
		diagnostic["line"] = 1;
		diagnostic["message"] = declaration_error;
		diagnostic["warning"] = false;
		diagnostics.push_back(diagnostic);
	}
	for (const Item &item : check->items) {
		if (declaration_result != OK) {
			break;
		}
		parsed += item.parsed;
		cached += !item.changed;
		changed |= item.changed;
		const Entry &entry = item.entry;
		const auto diagnostic = [&](int p_line, const String &p_message, bool p_warning) {
			output.push_back(vformat("%s:%d: %s: %s", item.path, p_line, p_warning ? "warning (MISSING_NEWLINE)" : "error", p_message));
			Dictionary entry_diagnostic;
			entry_diagnostic["path"] = item.path;
			entry_diagnostic["line"] = p_line;
			entry_diagnostic["message"] = p_message;
			entry_diagnostic["warning"] = p_warning;
			diagnostics.push_back(entry_diagnostic);
		};
		if (entry.syntax.error != OK) {
			errors++;
			diagnostic(entry.syntax.line, entry.syntax.message.is_empty() ? String(error_names[entry.syntax.error]) : entry.syntax.message, false);
		}
		for (int line : entry.syntax.missing_newlines) {
			warnings++;
			diagnostic(line, "Missing newline between resource records; accepted by Godot, but difficult to read.", true);
		}
	}
	if (changed && declaration_result == OK) {
		check->save_cache();
	}
	const int64_t elapsed = OS::get_singleton()->get_ticks_msec() - started;
	output.push_back(vformat("wgodot-check: checked %d scene/resource file(s), found %d error(s), %d warning(s) in %d ms.", check->items.size(), errors, warnings, elapsed));
	output.push_back(vformat("wgodot-check: resources parsed %d, cached %d.", parsed, cached));
	check->result["output"] = output;
	check->result["diagnostics"] = diagnostics;
	check->result["resource_count"] = check->items.size();
	check->result["resource_parsed_count"] = parsed;
	check->result["resource_cached_count"] = cached;
	check->result["resource_error_count"] = errors;
	check->result["resource_warning_count"] = warnings;
	check->result["resource_elapsed_msec"] = elapsed;
}

void WGodotResourceCheck::save_cache() const {
	if (cache_path.is_empty() || DirAccess::make_dir_recursive_absolute(cache_path.get_base_dir()) != OK) {
		return;
	}
	Dictionary entries;
	for (const Item &item : items) {
		if (item.entry.digest.is_empty()) {
			continue;
		}
		Dictionary entry;
		entry["digest"] = item.entry.digest;
		entry["error"] = item.entry.syntax.error;
		entry["line"] = item.entry.syntax.line;
		entry["message"] = item.entry.syntax.message;
		entry["missing_newlines"] = item.entry.syntax.missing_newlines;
		Dictionary dependencies;
		for (const auto &dependency : item.entry.syntax.dependencies) {
			dependencies[dependency.key] = dependency.value;
		}
		entry["dependencies"] = dependencies;
		Dictionary declarations;
		for (const auto &declaration : item.entry.syntax.declarations) {
			declarations[declaration.key] = declaration.value;
		}
		entry["declarations"] = declarations;
		entries[item.path] = entry;
	}
	Dictionary saved;
	saved["classes"] = class_identity;
	saved["entries"] = entries;
	const String temporary = cache_path + "." + itos(OS::get_singleton()->get_process_id()) + "." + uitos(Thread::get_caller_id()) + ".tmp";
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE);
	if (file.is_null()) {
		return;
	}
	file->store_var(saved, false);
	file->flush();
	const Error error = file->get_error();
	file.unref();
	if (error != OK || DirAccess::rename_absolute(temporary, cache_path) != OK) {
		DirAccess::remove_absolute(temporary);
	}
}

Dictionary WGodotResourceCheck::poll() {
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	if (task != WorkerThreadPool::INVALID_TASK_ID) {
		if (!pool->is_task_completed(task)) {
			return Dictionary();
		}
		pool->wait_for_task_completion(task);
		task = WorkerThreadPool::INVALID_TASK_ID;
		if (context != cache_context) {
			return result;
		}
		cache.clear();
		for (const Item &item : items) {
			cache.insert(item.path, item.entry);
		}
	}
	return result;
}

WGodotResourceCheck::~WGodotResourceCheck() {
	if (task != WorkerThreadPool::INVALID_TASK_ID) {
		cancelled.set();
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
	}
}
