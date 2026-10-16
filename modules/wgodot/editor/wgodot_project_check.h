// wgodot-changes::file

#pragma once

#include "wgodot_project_refresh.h"
#include "modules/gdscript/wgodot_gd/editor/gdscript_check_cli.h"

class WGodotProjectCheck {
	WGodotProjectRefresh refresh;
	Ref<WGodotGDScriptCheckCLI::ProjectCheck> check;

public:
	// An empty result means the refresh started successfully, or is still pending.
	Dictionary start();
	Dictionary poll();
};
