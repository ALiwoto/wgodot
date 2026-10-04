// wgodot-changes::file
#pragma once

#include "core/object/object_id.h"
#include "core/variant/dictionary.h"

// One asynchronous CLI request. Metric storage remains owned by EditorProfiler.
class WGodotProfiler {
	ObjectID debugger_id;
	uint64_t generation = 0;
	uint64_t deadline_msec = 0;
	Dictionary options;
	int session = -1;
	int target_frames = 0;
	bool owns_capture = false;
	bool stopping = false;
	bool timed_out = false;

public:
	Dictionary start(int p_session, const Dictionary &p_options);
	Dictionary poll();
	void cancel();
};
