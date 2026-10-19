// wgodot-changes::file
#include "animation_diagnostics.h"

#ifdef W_ANIMATION_CODEC_DIAGNOSTICS
#include "core/os/mutex.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/variant/variant.h"

WAnimationCodecProfile::WAnimationCodecProfile() {
	static const bool trace_enabled = OS::get_singleton()->get_environment("WGODOT_ANIMATION_CODEC_TRACE") == "1";
	enabled = trace_enabled;
	if (enabled) {
		started = phase_started = OS::get_singleton()->get_ticks_usec();
	}
}

void WAnimationCodecProfile::phase(Phase p_phase) {
	if (!enabled) {
		return;
	}
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	phase_usec[current_phase] += now - phase_started;
	phase_started = now;
	current_phase = p_phase;
}

void WAnimationCodecProfile::finish(const String &p_path, int p_version, Error p_status) {
	if (!enabled) {
		return;
	}
	phase(current_phase);
	const uint64_t elapsed = phase_started - started;
	const String report = vformat("ANIMATION_CODEC format=%d status=%d thread=%d total_usec=%d constants_usec=%d clocks_usec=%d tracks_usec=%d attach_usec=%d literals=%d references=%d constants=%d constant_table_bytes=%d clock_values=%d tracks=%d keys=%d key_bytes=%d path=%s",
			p_version, int(p_status), Thread::get_caller_id(), elapsed, phase_usec[CONSTANTS], phase_usec[CLOCKS], phase_usec[TRACKS], attach_usec, literals, references, constants, constant_table_bytes, clock_values, tracks, keys, key_bytes, p_path);
	// Log once per decode, outside measured work; never per key or via editor UI.
	static Mutex report_mutex;
	MutexLock lock(report_mutex);
	OS::get_singleton()->print("%s\n", report.utf8().get_data());
}
#endif
