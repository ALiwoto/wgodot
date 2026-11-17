// wgodot-changes::file

#pragma once

#include "wgodot_project_refresh.h"
#include "wgodot_resource_check.h"

#include "modules/gdscript/wgodot_gd/editor/gdscript_check_cli.h"

class WGodotProjectCheck {
	WGodotProjectRefresh refresh;
	Ref<WGodotGDScriptCheckCLI::ProjectCheck> check;
	Ref<WGodotResourceCheck> resources;
	std::shared_ptr<WGodotCheckBudget> budget;
	uint64_t started_msec = 0;

public:
	static Dictionary run_standalone(uint64_t p_timeout_msec, int p_errors_limit = 100);
	// An empty result means the refresh started successfully, or is still pending.
	Dictionary start(uint64_t p_timeout_msec = WGodotCLI::PROJECT_OPERATION_TIMEOUT_MSEC, int p_errors_limit = 100);
	Dictionary poll();
};
