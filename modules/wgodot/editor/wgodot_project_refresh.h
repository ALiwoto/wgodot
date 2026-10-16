// wgodot-changes::file

#pragma once

#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

class WGodotProjectRefresh {
	uint64_t deadline_msec = 0;
	bool refresh_scenes = true;
	static HashSet<String> changed_dependencies;

	void remember_scene_changes();
	Dictionary reload_scenes();

public:
	static void resources_changed(const Vector<String> &p_paths);
	// Empty results mean the refresh started successfully, or is still pending.
	Dictionary start(uint64_t p_timeout_msec = 60000, bool p_refresh_scenes = true);
	Dictionary poll();
};
