// wgodot-changes::file

#pragma once

#include "core/variant/dictionary.h"

class WGodotProjectCheck {
	uint64_t refresh_deadline_msec = 0;

public:
	// An empty result means the refresh started successfully, or is still pending.
	Dictionary start();
	Dictionary poll();
};
