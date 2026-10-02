// wgodot-changes::file

#include "wgodot_project_refresh.h"

#include "core/os/os.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"

namespace {

Dictionary failure(const String &p_error, const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["message"] = p_message;
	return result;
}

} // namespace

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
	Dictionary result;
	result["ok"] = true;
	result["refreshed"] = true;
	return result;
}
