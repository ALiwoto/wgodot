// wgodot-changes::file

#include "wgodot_project_check.h"

#include "core/os/os.h"
#include "core/variant/variant.h"

#include "modules/gdscript/wgodot_gd/editor/gdscript_check_cli.h"

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
	resources->start();
	return Dictionary();
}
