// wgodot-changes::file

#pragma once

#include "core/string/ustring.h"

// A thread-safe description of work which temporarily prevents editor commands.
// The CLI listener reads this without accessing editor objects on its I/O thread.
class WGodotEditorActivity {
	uint64_t id;

public:
	struct Snapshot {
		String stage;
		String path;
		uint64_t elapsed_msec = 0;
		bool busy = false;
	};

	WGodotEditorActivity(const String &p_stage, const String &p_path = String());
	~WGodotEditorActivity();
	WGodotEditorActivity(const WGodotEditorActivity &) = delete;
	WGodotEditorActivity &operator=(const WGodotEditorActivity &) = delete;
	static Snapshot get_snapshot();
};
