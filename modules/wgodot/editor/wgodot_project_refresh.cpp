// wgodot-changes::file

#include "wgodot_project_refresh.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/wgodot_resource_trace.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/wgodot_editor_activity.h"
#include "scene/gui/dialogs.h"
#include "scene/resources/packed_scene.h"

HashSet<String> WGodotProjectRefresh::changed_dependencies;

namespace {

Dictionary failure(const String &p_error, const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["message"] = p_message;
	return result;
}

bool depends_on_changes(const String &p_path, const HashSet<String> &p_changed, HashSet<String> &r_visited) {
	if (p_changed.has(p_path)) {
		return true;
	}
	if (r_visited.has(p_path)) {
		return false;
	}
	r_visited.insert(p_path);
	EditorFileSystemDirectory *directory = EditorFileSystem::get_singleton()->get_filesystem_path(p_path.get_base_dir());
	if (!directory) {
		return false;
	}
	const int index = directory->find_file_index(p_path.get_file());
	if (index < 0) {
		return false;
	}
	for (const String &dependency : directory->get_file_deps(index)) {
		if (depends_on_changes(dependency, p_changed, r_visited)) {
			return true;
		}
	}
	return false;
}

} // namespace

void WGodotProjectRefresh::resources_changed(const Vector<String> &p_paths) {
	for (const String &path : p_paths) {
		changed_dependencies.insert(path);
	}
}

void WGodotProjectRefresh::remember_scene_changes() {
	List<Ref<Resource>> cached;
	ResourceCache::get_cached_resources(&cached);
	for (const Ref<Resource> &resource : cached) {
		const String path = resource->get_path();
		if (!Object::cast_to<PackedScene>(resource.ptr()) || !path.is_resource_file() || !resource->get_import_path().is_empty()) {
			continue;
		}
		const uint64_t modified = FileAccess::get_modified_time(path);
		if (modified != 0 && modified != resource->get_last_modified_time()) {
			changed_dependencies.insert(path);
		}
	}
}

Dictionary WGodotProjectRefresh::reload_scenes() {
	WGodotResourceTrace trace("refresh.scenes");
	WGodotEditorActivity activity("Refreshing open scenes");
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		return failure("editor_unavailable", "The scene editor is unavailable.");
	}
	EditorData &data = EditorNode::get_editor_data();
	remember_scene_changes();
	HashSet<String> changed;
	List<Ref<Resource>> cached;
	ResourceCache::get_cached_resources(&cached);
	for (const Ref<Resource> &resource : cached) {
		const Ref<PackedScene> scene = resource;
		if (scene.is_null() || !scene->get_path().is_resource_file() || !scene->get_import_path().is_empty()) {
			continue;
		}
		const String path = scene->get_path();
		HashSet<String> visited;
		if (depends_on_changes(path, changed_dependencies, visited)) {
			changed.insert(path);
		}
	}
	// An open scene can still be stale even if its PackedScene was refreshed elsewhere.
	for (int i = 0; i < data.get_edited_scene_count(); i++) {
		const String path = data.get_scene_path(i);
		if (path.is_empty()) {
			continue;
		}
		const uint64_t modified = FileAccess::get_modified_time(path);
		HashSet<String> visited;
		if ((modified != 0 && modified != data.get_scene_modified_time(i)) || depends_on_changes(path, changed_dependencies, visited)) {
			changed.insert(path);
		}
	}
	Vector<String> paths;
	for (const String &path : changed) {
		paths.push_back(path);
	}
	paths.sort();
	Vector<String> tabs;
	PackedStringArray conflicts;
	for (int i = 0; i < data.get_edited_scene_count(); i++) {
		Node *root = data.get_edited_scene_root(i);
		if (!root) {
			continue;
		}
		const String path = data.get_scene_path(i);
		bool affected = changed.has(path);
		for (const String &dependency : paths) {
			if (affected) {
				break;
			}
			HashSet<Node *> instances;
			editor->find_all_instances_inheriting_path_in_node(root, root, dependency, instances);
			affected = !instances.is_empty();
		}
		if (!affected) {
			continue;
		}
		if (path.is_empty() || editor->is_scene_unsaved(i)) {
			conflicts.push_back(path.is_empty() ? data.get_scene_title(i) + " (unsaved scene)" : path);
		} else {
			tabs.push_back(path);
		}
	}
	if (!conflicts.is_empty()) {
		Dictionary result = failure("scene_reload_conflict", "These open scenes have unsaved changes and depend on files changed on disk. Save or resolve them in the editor before refreshing:\n" + String("\n").join(conflicts));
		result["conflicts"] = conflicts;
		return result;
	}
	if (!paths.is_empty()) {
		trace.phase("replace_cached_scenes");
		// Resource/script edits can require rebuilding instances even when the scene
		// itself is unchanged: soft script reload does not rerun _ready().
		for (const String &path : paths) {
			Error error = OK;
			const Ref<PackedScene> scene = ResourceLoader::load(path, "PackedScene", ResourceLoader::CACHE_MODE_REPLACE, &error);
			if (error != OK || scene.is_null()) {
				return failure("scene_reload_failed", "Cannot reload scene from disk: " + path);
			}
		}
		// Godot's editor reload restores the tab position and updates the scene tree.
		// Reload dependent tabs as well; replacing cached resources alone does not
		// reconstruct their already-instantiated nodes.
		for (const String &path : tabs) {
			WGodotResourceTrace tab_trace("refresh.scene_tab", path);
			editor->reload_scene(path);
			const int index = data.get_edited_scene_from_path(path);
			if (index < 0 || data.get_scene_modified_time(index) != FileAccess::get_modified_time(path)) {
				return failure("scene_reload_failed", "The scene editor did not finish reloading: " + path);
			}
		}
		if (editor->disk_changed->is_visible()) {
			editor->_scan_external_changes();
			if (editor->disk_changed_scenes.is_empty() && !editor->disk_changed_project) {
				editor->disk_changed->hide();
			}
		}
	}
	Dictionary result;
	result["ok"] = true;
	result["reloaded_scenes"] = paths;
	changed_dependencies.clear();
	return result;
}

Dictionary WGodotProjectRefresh::start() {
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (!filesystem) {
		return failure("filesystem_unavailable", "The editor filesystem is unavailable.");
	}
	deadline_msec = OS::get_singleton()->get_ticks_msec() + 60000;
	remember_scene_changes(); // Keep changes before the filesystem refresh replaces cached scenes.
	filesystem->scan_changes();
	return Dictionary();
}

Dictionary WGodotProjectRefresh::poll() {
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (!filesystem) {
		return failure("filesystem_unavailable", "The editor filesystem became unavailable.");
	}
	if (filesystem->is_scanning() || filesystem->is_importing()) {
		if (OS::get_singleton()->get_ticks_msec() > deadline_msec) {
			return failure("timeout", "Timed out while refreshing project files.");
		}
		return Dictionary();
	}
	// Reload external script changes without replacing unsaved editor buffers.
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (script_editor && script_editor->get_unsaved_files().is_empty()) {
		script_editor->reload_scripts();
	}
	Dictionary result = reload_scenes();
	if (!(bool)result.get("ok", false)) {
		return result;
	}
	result["refreshed"] = true;
	return result;
}
