// wgodot-changes::file
#include "../gdscript.h"
#include "../gdscript_function.h"

int GDScriptLanguage::profiling_get_data(ProfilingInfo *r_info_arr, int p_info_max, bool p_accumulated) {
	int current = 0;
#ifdef DEBUG_ENABLED
	MutexLock lock(mutex);
	GDScriptFunction::Profile::NativeCalls native_totals;
	// Reporting is read-only. Keep each caller's native time intact, including
	// when several callers invoke the same method or reports repeat mid-frame.
	auto collect_native = [&](const GDScriptFunction::Profile::NativeCalls &p_calls) {
		uint64_t caller_time = 0;
		for (const auto &entry : p_calls) {
			if (entry.value.call_count == 0) {
				continue;
			}
			caller_time += entry.value.total_time;
			auto *total = native_totals.getptr(entry.key);
			if (total) {
				total->call_count += entry.value.call_count;
				total->total_time += entry.value.total_time;
			} else {
				native_totals.insert(entry.key, entry.value);
			}
		}
		return caller_time;
	};
	SelfList<GDScriptFunction> *elem = p_accumulated ? function_list.first() : profile_last_frame_functions.first();
	while (elem) {
		const GDScriptFunction::Profile &profile = elem->self()->profile;
		const uint64_t calls = p_accumulated ? profile.call_count.get() : profile.last_frame_call_count;
		if (calls > 0) {
			uint64_t native_time = collect_native(p_accumulated ? profile.accumulated_native_calls : profile.last_native_calls);
			if (p_accumulated) {
				// Include the partial frame without flushing its counters. frame()
				// remains the only owner of the accumulation/rollover operation.
				native_time += collect_native(profile.native_calls);
			}
			if (current < p_info_max) {
				ProfilingInfo &info = r_info_arr[current++];
				info.signature = profile.signature;
				info.call_count = calls;
				info.self_time = p_accumulated ? profile.self_time.get() : profile.last_frame_self_time;
				info.total_time = p_accumulated ? profile.total_time.get() : profile.last_frame_total_time;
				info.internal_time = native_time;
			}
		}
		elem = elem->next();
	}
	for (const auto &entry : native_totals) {
		if (current >= p_info_max) {
			break;
		}
		ProfilingInfo &info = r_info_arr[current++];
		info.signature = entry.value.signature;
		info.call_count = entry.value.call_count;
		info.self_time = entry.value.total_time;
		info.total_time = entry.value.total_time;
		info.internal_time = entry.value.total_time;
	}
#endif
	return current;
}

