// wgodot-changes::file
#include "gpu_profile.h"

#ifdef TOOLS_ENABLED
#include "core/object/callable_mp.h"
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"
#include "servers/rendering/rendering_server_globals.h"
#include "servers/rendering/storage/utilities.h"

namespace WGodotGPUProfile {
namespace {

// Bounded diagnostic storage. Reaching a limit is reported, never silently
// overwrites earlier evidence. perf read drains these buffers during a run.
constexpr int MAX_TIMESTAMPS = 262144;
constexpr int MAX_EVENTS = 65536;
SafeFlag enabled;
Mutex mutex;
Result result;
int timestamp_count = 0;

void set_timestamps(bool p_enabled) {
	RSG::utilities->diagnostic_timestamps = p_enabled;
}

void append_event(CPUEvent p_event) {
	MutexLock lock(mutex);
	if (!enabled.is_set() || p_event.begin_usec < result.started_usec) {
		return;
	}
	if (result.events.size() == MAX_EVENTS) {
		result.truncated = true;
		return;
	}
	result.events.push_back(std::move(p_event));
}

} // namespace

bool is_enabled() {
	return enabled.is_set();
}

void start() {
	{
		MutexLock lock(mutex);
		result = Result();
		result.started_usec = OS::get_singleton()->get_ticks_usec();
		timestamp_count = 0;
		enabled.set();
	}
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(set_timestamps).bind(true));
}

Result take(bool p_stop) {
	Result output;
	{
		MutexLock lock(mutex);
		if (p_stop) {
			enabled.clear();
		}
		output = std::move(result);
		output.stopped_usec = OS::get_singleton()->get_ticks_usec();
		result = Result();
		// In-flight queries/scopes can complete after perf read. Retain the
		// session boundary so those records arrive in the next drained report.
		result.started_usec = output.started_usec;
		timestamp_count = 0;
	}
	if (p_stop) {
		RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(set_timestamps).bind(false));
	}
	return output;
}

void cancel() {
	if (enabled.is_set()) {
		take(true);
	}
}

void resolved_queries(RenderingDevice *p_device) {
	if (!enabled.is_set() || p_device != RenderingDevice::get_singleton()) {
		return;
	}
	MutexLock lock(mutex);
	if (!enabled.is_set()) {
		return;
	}
	const uint32_t count = p_device->get_captured_timestamps_count();
	if (timestamp_count + count > MAX_TIMESTAMPS) {
		result.truncated = true;
		return;
	}
	QueryBatch batch;
	for (uint32_t i = 0; i < count; i++) {
		const uint64_t cpu_usec = p_device->get_captured_timestamp_cpu_time(i);
		if (cpu_usec < result.started_usec) {
			continue;
		}
		Timestamp stamp;
		stamp.name = p_device->get_captured_timestamp_name(i);
		stamp.cpu_usec = cpu_usec;
		stamp.gpu_nsec = p_device->get_captured_timestamp_gpu_time(i);
		batch.timestamps.push_back(std::move(stamp));
	}
	if (!batch.timestamps.is_empty()) {
		timestamp_count += batch.timestamps.size();
		result.batches.push_back(std::move(batch));
	}
}

void mark(const char *p_name, const String &p_detail, int64_t p_sample) {
	if (!enabled.is_set()) {
		return;
	}
	CPUEvent event;
	event.name = p_name;
	event.detail = p_detail;
	event.sample = p_sample;
	event.thread = Thread::get_caller_id();
	event.begin_usec = OS::get_singleton()->get_ticks_usec();
	event.end_usec = event.begin_usec;
	append_event(std::move(event));
}

void interval(const String &p_name, uint64_t p_begin, uint64_t p_end) {
	if (!enabled.is_set() || !p_begin) {
		return;
	}
	CPUEvent event;
	event.name = p_name;
	event.thread = Thread::get_caller_id();
	event.begin_usec = p_begin;
	event.end_usec = p_end;
	append_event(std::move(event));
}

Scope::Scope(const char *p_name, const String &p_capture, int64_t p_sample) : name(p_name) {
	if (enabled.is_set()) {
		started_usec = OS::get_singleton()->get_ticks_usec();
		capture = p_capture;
		sample = p_sample;
	}
}

Scope::~Scope() {
	finish();
}

void Scope::finish() {
	if (!started_usec || !enabled.is_set()) {
		return;
	}
	CPUEvent event;
	event.end_usec = OS::get_singleton()->get_ticks_usec();
	event.begin_usec = started_usec;
	event.thread = Thread::get_caller_id();
	event.name = name;
	event.detail = capture;
	event.sample = sample;
	started_usec = 0;
	append_event(std::move(event));
}

Dictionary report(const Result &p_result) {
	Dictionary output;
	output["process_id"] = int64_t(OS::get_singleton()->get_process_id());
	output["started_usec"] = int64_t(p_result.started_usec);
	output["stopped_usec"] = int64_t(p_result.stopped_usec);
	output["truncated"] = p_result.truncated;
	Array batches;
	for (const QueryBatch &batch : p_result.batches) {
		Array timestamps;
		for (const Timestamp &stamp : batch.timestamps) {
			Dictionary row;
			row["name"] = stamp.name;
			row["cpu_usec"] = int64_t(stamp.cpu_usec);
			row["gpu_nsec"] = int64_t(stamp.gpu_nsec);
			timestamps.push_back(row);
		}
		batches.push_back(timestamps);
	}
	output["query_batches"] = batches;
	Array events;
	for (const CPUEvent &event : p_result.events) {
		Dictionary row;
		row["name"] = event.name;
		row["thread"] = int64_t(event.thread);
		row["begin_usec"] = int64_t(event.begin_usec);
		row["end_usec"] = int64_t(event.end_usec);
		if (event.sample >= 0) {
			row["sample"] = event.sample;
			row["capture"] = event.detail;
		}
		events.push_back(row);
	}
	output["cpu_events"] = events;
	output["note"] = "GPU timestamps are device nanoseconds; CPU timestamps are process-clock microseconds. Do not subtract the two clocks. Query batches include mid-frame flushes. GPU intervals measure elapsed command time, not utilization, and may include GPU starvation. CPU scopes can overlap. Only already-resolved queries are read: the trailing GPU work may still be pending at stop. Drain with perf read during long captures. No additional GPU waits or per-frame disk writes are introduced.";
	return output;
}

} // namespace WGodotGPUProfile
#endif
