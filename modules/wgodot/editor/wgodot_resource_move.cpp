// wgodot-changes::file
#include "wgodot_resource_move.h"

#include "wgodot_project_refresh.h"
#include "wgodot_resource_rewrite.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/wgodot_editor_activity.h"

namespace {

Dictionary failure(const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = "resource_move_failed";
	result["message"] = p_message;
	return result;
}

String project_path(const String &p_path) {
	String path = p_path.replace("\\", "/");
	if (path.is_relative_path()) {
		path = "res://" + path;
	}
	return ProjectSettings::get_singleton()->localize_path(path).simplify_path().trim_suffix("/");
}

void collect_files(EditorFileSystemDirectory *p_directory, Vector<String> &r_files) {
	for (int i = 0; i < p_directory->get_file_count(); i++) {
		r_files.push_back(p_directory->get_file_path(i));
	}
	for (int i = 0; i < p_directory->get_subdir_count(); i++) {
		collect_files(p_directory->get_subdir(i), r_files);
	}
}

} // namespace

Dictionary WGodotResourceMove::execute(const Dictionary &p_options) {
	WGodotEditorActivity activity("Moving project resources");
	const PackedStringArray paths = p_options.get("paths", PackedStringArray());
	if (paths.is_empty() || paths.size() % 2 != 0) {
		return failure("Supply source/destination pairs.");
	}
	ScriptEditor *scripts = ScriptEditor::get_singleton();
	if (scripts && !scripts->get_unsaved_files().is_empty()) {
		return failure("Save scripts open in the editor before moving resources.");
	}
	EditorNode *editor = EditorNode::get_singleton();
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	FileSystemDock *dock = FileSystemDock::get_singleton();
	Vector<FileSystemDock::FileOrFolder> items;
	Vector<String> destinations;
	HashMap<String, String> planned_files;
	HashSet<String> source_files;
	Array moves;
	for (int i = 0; i < paths.size(); i += 2) {
		const String source = project_path(paths[i]);
		const String destination = project_path(paths[i + 1]);
		if (!source.begins_with("res://") || !destination.begins_with("res://") || source == "res://" || destination == "res://") {
			return failure("Moves must stay inside the project: " + paths[i] + " -> " + paths[i + 1]);
		}
		if (!destination.get_file().is_valid_filename()) {
			return failure("Invalid destination filename: " + destination);
		}
		const bool is_file = FileAccess::exists(source);
		if (!is_file && !filesystem->get_filesystem_path(source)) {
			return failure("Source is not in the editor filesystem: " + source);
		}
		if (destination == source || (!is_file && destination.begins_with(source + "/"))) {
			return failure("Cannot move an item into itself: " + source);
		}
		if (FileAccess::exists(destination) || DirAccess::dir_exists_absolute(destination) || FileAccess::exists(destination + ".import") || FileAccess::exists(destination + ".uid")) {
			return failure("Destination already exists: " + destination);
		}
		for (int j = 0; j < items.size(); j++) {
			if (source == items[j].path || source.begins_with(items[j].path + "/") || items[j].path.begins_with(source + "/") || destination == destinations[j] || destination.begins_with(destinations[j] + "/") || destinations[j].begins_with(destination + "/")) {
				return failure("Overlapping source or destination paths must be moved in separate commands.");
			}
		}
		items.push_back(FileSystemDock::FileOrFolder(source, is_file));
		destinations.push_back(destination);
		Vector<String> files;
		if (is_file) {
			files.push_back(source);
		} else {
			collect_files(filesystem->get_filesystem_path(source), files);
		}
		for (const String &file : files) {
			planned_files[file] = is_file ? destination : destination + file.substr(source.length());
			source_files.insert(file);
		}
		Dictionary move;
		move["source"] = source;
		move["destination"] = destination;
		moves.push_back(move);
	}
	for (const String &destination : destinations) {
		for (const FileSystemDock::FileOrFolder &item : items) {
			if (destination == item.path || destination.begins_with(item.path + "/")) {
				return failure("A destination cannot be inside another source in the same command.");
			}
		}
	}

	HashSet<String> owners;
	dock->_find_file_owners(filesystem->get_filesystem(), source_files, owners);
	// Deferred WResPath values are ordinary serialized strings, so the native
	// dependency index does not include them. Reuse the exporter's token mapper.
	HashMap<String, String> rewritten;
	Vector<String> files;
	collect_files(filesystem->get_filesystem(), files);
	const int resource_count = files.size();
	for (int i = 0; i < resource_count; i++) {
		if (FileAccess::exists(files[i] + ".import")) {
			files.push_back(files[i] + ".import");
		}
	}
	for (const String &file : files) {
		const String extension = file.get_extension();
		if (extension != "tscn" && extension != "tres" && extension != "gd" && extension != "gdshader" && extension != "gdshaderinc" && extension != "import") {
			continue;
		}
		Error error = OK;
		const String original = FileAccess::get_file_as_string(file, &error);
		if (error != OK) {
			return failure("Cannot read resource references: " + file);
		}
		const String changed = WGodotResourceRewrite::text(original, file, [&](const String &p_path) {
			const String *replacement = planned_files.getptr(p_path);
			return replacement ? *replacement : p_path;
		});
		if (changed != original) {
			rewritten[file] = changed;
			if (extension != "import") {
				owners.insert(file);
			}
		}
	}
	EditorData &data = EditorNode::get_editor_data();
	for (int i = 0; i < data.get_edited_scene_count(); i++) {
		if (editor->is_scene_unsaved(i)) {
			return failure("Save open scenes before moving resources: " + data.get_scene_title(i));
		}
	}
	Dictionary result;
	result["ok"] = true;
	result["moves"] = moves;
	result["resource_count"] = planned_files.size();
	result["updated_files"] = owners.size();
	result["dry_run"] = p_options.get("dry_run", false);
	if ((bool)result["dry_run"]) {
		return result;
	}

	for (const String &destination : destinations) {
		const Error error = DirAccess::make_dir_recursive_absolute(destination.get_base_dir());
		if (error != OK) {
			return failure("Cannot create destination directory: " + destination.get_base_dir());
		}
	}
	HashMap<String, String> file_renames;
	HashMap<String, String> folder_renames;
	for (int i = 0; i < items.size(); i++) {
		if (items[i].is_file && filesystem->is_group_file(items[i].path)) {
			filesystem->move_group_file(items[i].path, destinations[i]);
		}
		dock->_try_move_item(items[i], destinations[i], file_renames, folder_renames);
		if (!FileAccess::exists(destinations[i]) && !DirAccess::dir_exists_absolute(destinations[i])) {
			return failure("Move failed; inspect previously moved paths before retrying: " + items[i].path);
		}
	}
	Vector<String> changed_files;
	for (const KeyValue<String, String> &entry : rewritten) {
		const bool import_settings = entry.key.ends_with(".import");
		const String source = import_settings ? entry.key.trim_suffix(".import") : entry.key;
		const String *moved_path = file_renames.getptr(source);
		const String path = moved_path ? *moved_path + (import_settings ? ".import" : "") : entry.key;
		Error error = OK;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &error);
		if (error != OK) {
			return failure("Files moved, but references could not be written: " + path);
		}
		file->store_string(entry.value);
		file->flush();
		if (file->get_error() != OK) {
			return failure("Error saving moved resource references: " + path);
		}
		changed_files.push_back(path);
	}
	const int current_tab = EditorSceneTabs::get_singleton()->get_current_tab();
	dock->_update_resource_paths_after_move(file_renames);
	List<Ref<Resource>> cached;
	ResourceCache::get_cached_resources(&cached);
	for (const Ref<Resource> &resource : cached) {
		if (changed_files.has(resource->get_path()) && resource->get_path().get_extension() == "tres") {
			resource->reload_from_file();
		}
	}
	dock->_update_dependencies_after_move(file_renames, owners);
	dock->_update_project_settings_after_move(file_renames, folder_renames);
	dock->_update_favorites_after_move(file_renames, folder_renames);
	EditorSceneTabs::get_singleton()->set_current_tab(current_tab);
	WGodotProjectRefresh::resources_changed(changed_files);
	// The CLI starts one changes scan and waits for it after this returns.
	// A simultaneous full scan can finish its worker thread before its main
	// thread has installed the new filesystem and imported the moved assets.
	return result;
}
