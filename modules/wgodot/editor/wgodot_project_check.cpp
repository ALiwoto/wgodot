// wgodot-changes::file

#include "wgodot_project_check.h"

#include "core/variant/variant.h"

#include "modules/gdscript/wgodot_gd/editor/gdscript_check_cli.h"

Dictionary WGodotProjectCheck::start() {
	return refresh.start();
}

Dictionary WGodotProjectCheck::poll() {
	const Dictionary refreshed = refresh.poll();
	if (refreshed.is_empty() || !(bool)refreshed.get("ok", false)) {
		return refreshed;
	}
	Dictionary result = WGodotGDScriptCheckCLI::run_project_check_result();
	result["refreshed"] = true;
	return result;
}
