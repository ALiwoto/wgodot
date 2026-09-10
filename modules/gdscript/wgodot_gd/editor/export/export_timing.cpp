// wgodot-changes::file
/**************************************************************************/
/*  export_timing.cpp                                                     */
/**************************************************************************/

#include "export_timing.h"

#include "core/error/error_macros.h"
#include "core/os/os.h"
#include "core/os/time.h"

namespace WGodotGDScriptExportTransform {

namespace {

ObfuscationNameProbe obfuscation_name_probe;
bool obfuscation_name_probe_enabled = false;

String get_utc_timestamp() {
	return Time::get_singleton() != nullptr ? Time::get_singleton()->get_datetime_string_from_system(true, true) + "Z" : String("unknown");
}

} // namespace

uint64_t export_timing_get_ticks_usec() {
	return OS::get_singleton() != nullptr ? OS::get_singleton()->get_ticks_usec() : 0;
}

String export_timing_format_msec(uint64_t p_usec) {
	return String::num(static_cast<double>(p_usec) / 1000.0, 3);
}

String export_timing_prefix() {
	return "[WGodot export timing][utc=" + get_utc_timestamp() + "] ";
}

bool export_timing_should_log(const TransformOptions &p_options) {
	return p_options.timing_logs_enabled;
}

bool export_timing_should_log_verbose(const TransformOptions &p_options) {
	return p_options.timing_logs_enabled && p_options.timing_verbose_logs_enabled;
}

uint64_t export_timing_slow_threshold_usec(const TransformOptions &p_options) {
	return static_cast<uint64_t>(MAX(p_options.timing_slow_threshold_msec, 0)) * 1000;
}

void export_timing_log_checkpoint(const TransformOptions &p_options, const String &p_context, const String &p_phase) {
	if (!export_timing_should_log_verbose(p_options)) {
		return;
	}

	WARN_PRINT(export_timing_prefix() + p_context + " " + p_phase + ".");
}

void export_timing_log_slow_phase(const TransformOptions &p_options, const String &p_context, const String &p_phase, uint64_t p_usec, uint64_t p_threshold_usec) {
	if (!export_timing_should_log(p_options)) {
		return;
	}

	const uint64_t threshold_usec = p_threshold_usec > 0 ? p_threshold_usec : export_timing_slow_threshold_usec(p_options);
	if (p_usec < threshold_usec) {
		return;
	}

	WARN_PRINT(export_timing_prefix() + vformat("%s %s took %s ms.", p_context, p_phase, export_timing_format_msec(p_usec)));
}

void reset_obfuscation_name_probe() {
	obfuscation_name_probe = ObfuscationNameProbe();
}

ObfuscationNameProbe get_obfuscation_name_probe() {
	return obfuscation_name_probe;
}

void set_obfuscation_name_probe_enabled(bool p_enabled) {
	obfuscation_name_probe_enabled = p_enabled;
}

bool is_obfuscation_name_probe_enabled() {
	return obfuscation_name_probe_enabled;
}

void record_obfuscation_name_probe(int p_attempts, uint64_t p_usec) {
	if (!obfuscation_name_probe_enabled) {
		return;
	}

	obfuscation_name_probe.calls++;
	obfuscation_name_probe.attempts += p_attempts;
	obfuscation_name_probe.collisions += MAX(p_attempts - 1, 0);
	obfuscation_name_probe.max_attempts = MAX(obfuscation_name_probe.max_attempts, p_attempts);
	obfuscation_name_probe.usec += p_usec;
}

} // namespace WGodotGDScriptExportTransform
