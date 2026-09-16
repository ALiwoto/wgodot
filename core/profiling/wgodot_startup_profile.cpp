// wgodot-changes::file

#include "wgodot_startup_profile.h"

#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/string/print_string.h"
#include "core/variant/variant.h"

#include <atomic>

namespace WGodotStartupProfile {

static std::atomic<bool> enabled = false;
static uint64_t origin_usec = 0;
thread_local Scope *Scope::current = nullptr;

void enable() {
	origin_usec = OS::get_singleton()->get_ticks_usec();
	enabled.store(true);
	print_line("[wgodot-startup] Enabled; timings stop after the first main-loop iteration.");
}

Scope::Scope(const char *p_operation, const String &p_detail, bool p_stop_after) :
		stop_after(p_stop_after) {
	if (!enabled.load()) {
		return;
	}
	detail = p_detail;
	begin(p_operation);
}

void Scope::begin(const char *p_operation) {
	operation = p_operation;
	parent = current;
	children_usec = 0;
	started_usec = OS::get_singleton()->get_ticks_usec();
	current = this;
}

Scope::~Scope() {
	finish();
}

void Scope::next(const char *p_operation) {
	if (!operation) {
		return;
	}
	finish();
	if (enabled.load()) {
		begin(p_operation);
	}
}

void Scope::finish() {
	if (!operation) {
		return;
	}
	const uint64_t duration_usec = OS::get_singleton()->get_ticks_usec() - started_usec;
	current = parent;
	if (enabled.load() && duration_usec >= 1000) {
		print_line(vformat("[wgodot-startup] thread=%d start=%.3f ms total=%.3f ms self=%.3f ms %s %s",
				Thread::get_caller_id(), (started_usec - origin_usec) / 1000.0,
				duration_usec / 1000.0, (duration_usec - children_usec) / 1000.0, operation, detail));
	}
	if (parent) {
		// Attribute this scope's logging overhead to the child as well, so it
		// does not appear as unexplained self time in its parent.
		parent->children_usec += OS::get_singleton()->get_ticks_usec() - started_usec;
	}
	operation = nullptr;
	if (stop_after) {
		enabled.store(false);
		print_line("[wgodot-startup] First main-loop iteration finished; profiling disabled.");
	}
}

} // namespace WGodotStartupProfile
