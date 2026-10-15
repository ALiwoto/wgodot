// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

// OS notifications only collect paths. EditorFileSystem owns reconciliation.
class WGodotFileSystemWatcher {
	struct State;
	State *state = nullptr;

public:
	struct Changes {
		HashSet<String> paths;
		bool available = false;
		bool rescan_all = false;
	};

	void start(const String &p_root);
	Changes take_changes();
	~WGodotFileSystemWatcher();
};
