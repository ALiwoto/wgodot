// wgodot-changes::file

#include "wgodot_project_check.h"

#include "core/variant/variant.h"

#include "modules/gdscript/wgodot_gd/editor/gdscript_check_cli.h"

Dictionary WGodotProjectCheck::start() {
	return refresh.start(60000, false);
}

Dictionary WGodotProjectCheck::poll() {
	if (check.is_valid()) {
		return check->poll();
	}
	const Dictionary refreshed = refresh.poll();
	if (refreshed.is_empty() || !(bool)refreshed.get("ok", false)) {
		return refreshed;
	}
	check.instantiate();
	check->start();
	return Dictionary();
}
