// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

class RenderingDevice;

namespace WGodotGPUProfile {

struct Timestamp {
	String name;
	uint64_t cpu_usec = 0;
	uint64_t gpu_nsec = 0;
};

struct QueryBatch {
	Vector<Timestamp> timestamps;
};

struct CPUEvent {
	String name;
	String detail;
	uint64_t thread = 0;
	uint64_t begin_usec = 0;
	uint64_t end_usec = 0;
	int64_t sample = -1;
};

struct Result {
	Vector<QueryBatch> batches;
	Vector<CPUEvent> events;
	uint64_t started_usec = 0;
	uint64_t stopped_usec = 0;
	bool truncated = false;
};

// No GPU waits or file writes. Consume only queries the device has already
// resolved, including batches completed by mid-frame readback flushes.
bool is_enabled();
void start();
Result take(bool p_stop);
void cancel();
void resolved_queries(RenderingDevice *p_device);
void mark(const char *p_name, const String &p_detail, int64_t p_sample);
Dictionary report(const Result &p_result);

class Scope {
	const char *name;
	uint64_t started_usec = 0;

public:
	explicit Scope(const char *p_name);
	~Scope();
};

} // namespace WGodotGPUProfile
#endif
