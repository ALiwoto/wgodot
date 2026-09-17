// wgodot-changes::file

#include "wgodot_project_check.h"

#include "core/os/os.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/script/script_editor_plugin.h"

#include "modules/gdscript/wgodot_gd/editor/gdscript_check_cli.h"

namespace {

Dictionary make_error_result(const String &p_error, const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["error"] = p_error;
	result["message"] = p_message;
	return result;
}

} // namespace

Dictionary WGodotProjectCheck::start() {
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (filesystem == nullptr) {
		return make_error_result("filesystem_unavailable", "The editor filesystem is unavailable.");
	}
	refresh_deadline_msec = OS::get_singleton()->get_ticks_msec() + 60000;
	filesystem->scan_changes();
	return Dictionary();
}

Dictionary WGodotProjectCheck::poll() {
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (filesystem == nullptr) {
		return make_error_result("filesystem_unavailable", "The editor filesystem became unavailable.");
	}
	if (filesystem->is_scanning() || filesystem->is_importing()) {
		if (OS::get_singleton()->get_ticks_msec() > refresh_deadline_msec) {
			return make_error_result("timeout", "Timed out while refreshing project files for analysis.");
		}
		return Dictionary();
	}

	// EditorFileSystem leaves open scripts to ScriptEditor. Reload external
	// changes only when doing so cannot overwrite an unsaved editor buffer.
	ScriptEditor *script_editor = ScriptEditor::get_singleton();
	if (script_editor != nullptr && script_editor->get_unsaved_files().is_empty()) {
		script_editor->reload_scripts();
	}
	Dictionary result = WGodotGDScriptCheckCLI::run_project_check_result();
	result["refreshed"] = true;
	return result;
}
