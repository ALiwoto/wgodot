// wgodot-changes::file

#pragma once

#include "core/variant/dictionary.h"

class WGodotProjectRefresh {
	uint64_t deadline_msec = 0;

	Dictionary reload_scenes();

public:
	// Empty results mean the refresh started successfully, or is still pending.
	Dictionary start();
	Dictionary poll();
};
