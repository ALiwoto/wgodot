// wgodot-changes::file

#include "wgodot_project_check.h"

#include "wgodot_cpp_project.h"

#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "core/variant/variant.h"

#include "modules/gdscript/wgodot_gd/editor/gdscript_check_cli.h"

namespace {

Error collect_inputs(const String &p_path, Vector<String> &r_scripts, Vector<String> &r_resources, String &r_error) {
	Error error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_path, &error);
	if (error != OK) {
		r_error = vformat("Cannot read %s: %s", p_path, error_names[error]);
		return error;
	}
	if (directory->file_exists(".gdignore")) {
		return OK;
	}
	error = directory->list_dir_begin();
	if (error != OK) {
		r_error = vformat("Cannot list %s: %s", p_path, error_names[error]);
		return error;
	}
	for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
		if (name.begins_with(".")) {
			continue;
		}
		const String path = p_path.path_join(name);
		if (directory->current_is_dir()) {
			if (directory->is_link(name)) {
				r_error = "Standalone checks do not support directory symlinks: " + path;
				return ERR_UNAVAILABLE;
			}
			RETURN_IF_ERROR(collect_inputs(path, r_scripts, r_resources, r_error));
		} else if (name.get_extension() == "gd") {
			r_scripts.push_back(path);
		} else if (name.get_extension() == "tscn" || name.get_extension() == "tres") {
			r_resources.push_back(path);
		}
	}
	return OK;
}

Dictionary failure(const String &p_message) {
	Dictionary result;
	result["ok"] = false;
	result["message"] = p_message;
	return result;
}

} // namespace

Dictionary WGodotProjectCheck::run_standalone(uint64_t p_timeout_msec) {
	WGodotProjectCheck operation;
	operation.started_msec = OS::get_singleton()->get_ticks_msec();
	Vector<String> scripts;
	Vector<String> resource_paths;
	String error;
	if (collect_inputs("res://", scripts, resource_paths, error) != OK) {
		return failure(error);
	}
	scripts.sort();
	if (WGodotCppProject::refresh_script_classes(scripts) != OK) {
		return failure("Cannot refresh script classes for the standalone check.");
	}
	operation.check.instantiate();
	operation.resources.instantiate();
	operation.check->start(scripts);
	operation.resources->start(resource_paths, operation.check->get_project());
	while (OS::get_singleton()->get_ticks_msec() - operation.started_msec < p_timeout_msec) {
		Dictionary result = operation.poll();
		if (!result.is_empty()) {
			result["refreshed"] = false;
			return result;
		}
		OS::get_singleton()->delay_usec(1000);
	}
	return failure("Standalone project check timed out.");
}

Dictionary WGodotProjectCheck::start(uint64_t p_timeout_msec) {
	return refresh.start(p_timeout_msec, false);
}

Dictionary WGodotProjectCheck::poll() {
	if (check.is_valid()) {
		Dictionary result = check->poll();
		const Dictionary resource_result = resources->poll();
		if (result.is_empty() || resource_result.is_empty()) {
			return Dictionary();
		}
		PackedStringArray output = result.get("output", PackedStringArray());
		output.append_array(resource_result["output"]);
		Array diagnostics = result.get("diagnostics", Array());
		diagnostics.append_array(resource_result["diagnostics"]);
		result.merge(resource_result);
		result["output"] = output;
		result["diagnostics"] = diagnostics;
		result["error_count"] = int(result.get("error_count", 0)) + int(resource_result["resource_error_count"]);
		result["warning_count"] = int(result.get("warning_count", 0)) + int(resource_result["resource_warning_count"]);
		if (int(resource_result["resource_error_count"]) != 0) {
			result["exit_code"] = 1;
		}
		result["elapsed_msec"] = int64_t(OS::get_singleton()->get_ticks_msec() - started_msec);
		return result;
	}
	const Dictionary refreshed = refresh.poll();
	if (refreshed.is_empty() || !(bool)refreshed.get("ok", false)) {
		return refreshed;
	}
	check.instantiate();
	resources.instantiate();
	started_msec = OS::get_singleton()->get_ticks_msec();
	check->start();
	resources->start(check->get_project());
	return Dictionary();
}
