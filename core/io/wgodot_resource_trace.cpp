// wgodot-changes::file

#include "wgodot_resource_trace.h"

#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/string/print_string.h"

namespace {
Mutex trace_mutex;
uint64_t next_trace_id = 0;
} // namespace

WGodotResourceTrace::WGodotResourceTrace(const String &p_operation, const String &p_path) {
#ifdef TOOLS_ENABLED
	static const bool enabled = OS::get_singleton()->get_environment("WGODOT_TRACE_RESOURCES") == "1";
	if (!enabled) {
		return;
	}
	started = OS::get_singleton()->get_ticks_usec();
	MutexLock lock(trace_mutex);
	id = ++next_trace_id;
	print_line(vformat("RESOURCE_TRACE\tbegin\t%d\t%d\t%d\t%s\t%s", id, started, Thread::get_caller_id(), p_operation, p_path));
#endif
}

WGodotResourceTrace::~WGodotResourceTrace() {
	if (id) {
		const uint64_t ended = OS::get_singleton()->get_ticks_usec();
		MutexLock lock(trace_mutex);
		print_line(vformat("RESOURCE_TRACE\tend\t%d\t%d\t%d", id, ended, ended - started));
	}
}

void WGodotResourceTrace::phase(const String &p_phase) const {
	if (id) {
		const uint64_t now = OS::get_singleton()->get_ticks_usec();
		MutexLock lock(trace_mutex);
		print_line(vformat("RESOURCE_TRACE\tphase\t%d\t%d\t%s", id, now, p_phase));
	}
}
