// wgodot-changes::file
#include "wgodot_dependency_errors.h"

#include "core/object/callable_mp.h"
#include "core/object/message_queue.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/vector.h"

namespace WGodotDependencyErrors {

namespace {
Mutex mutex;
Vector<Callable> pending;
} // namespace

void queue(const Callable &p_report) {
	MutexLock lock(mutex);
	if (pending.is_empty()) {
		MessageQueue::get_main_singleton()->push_callable(callable_mp_static(&flush));
	}
	pending.push_back(p_report);
}

void flush() {
	DEV_ASSERT(Thread::is_main_thread());
	Vector<Callable> reports;
	{
		MutexLock lock(mutex);
		reports = pending;
		pending.clear();
	}
	for (const Callable &report : reports) {
		report.call();
	}
}

} // namespace WGodotDependencyErrors
