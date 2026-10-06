// wgodot-changes::file

#include "wgodot_project_refresh.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/gui/dialogs.h"
#include "scene/resources/packed_scene.h"

namespace {

Dictionary failure(const String &p_error, const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["message"] = p_message;
	return result;
}

} // namespace

Dictionary WGodotProjectRefresh::reload_scenes() {
	EditorNode *editor = EditorNode::get_singleton();
	if (!editor) {
		return failure("editor_unavailable", "The scene editor is unavailable.");
	}
	EditorData &data = EditorNode::get_editor_data();
	HashSet<String> changed;
	List<Ref<Resource>> cached;
	ResourceCache::get_cached_resources(&cached);
	for (const Ref<Resource> &resource : cached) {
		const Ref<PackedScene> scene = resource;
		if (scene.is_null() || !scene->get_path().is_resource_file() || !scene->get_import_path().is_empty()) {
			continue;
		}
		const String path = scene->get_path();
		const uint64_t modified = FileAccess::get_modified_time(path);
		if (modified != 0 && modified != scene->get_last_modified_time()) {
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
		if (modified != 0 && modified != data.get_scene_modified_time(i)) {
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
		Dictionary result = failure("scene_reload_conflict", "These open scenes have unsaved changes and are affected by scene files changed on disk. Save or resolve them in the editor before refreshing:\n" + String("\n").join(conflicts));
		result["conflicts"] = conflicts;
		return result;
	}
	if (!paths.is_empty()) {
		// Refresh every changed PackedScene before reopening tabs, so nested scenes
		// and script preloads also see the current objects through the normal cache.
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
	return result;
}

Dictionary WGodotProjectRefresh::start() {
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (!filesystem) {
		return failure("filesystem_unavailable", "The editor filesystem is unavailable.");
	}
	deadline_msec = OS::get_singleton()->get_ticks_msec() + 60000;
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
