// wgodot-changes::file

#pragma once

#include "wgodot_project_refresh.h"

class WGodotProjectCheck {
	WGodotProjectRefresh refresh;

public:
	// An empty result means the refresh started successfully, or is still pending.
	Dictionary start();
	Dictionary poll();
};
