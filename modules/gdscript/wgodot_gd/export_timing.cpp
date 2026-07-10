// wgodot-changes::file
/**************************************************************************/
/*  export_timing.cpp                                                     */
/**************************************************************************/

#include "export_timing.h"

#include "source_rewrite.h"

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

bool should_log_transform_timing(const TransformOptions &p_options, const ExportTransformTimingStats &p_timing) {
	if (!export_timing_should_log(p_options)) {
		return false;
	}

	const uint64_t threshold_usec = export_timing_slow_threshold_usec(p_options);
	return p_timing.total_usec >= threshold_usec ||
			p_timing.collect_replacements_usec >= threshold_usec ||
			p_timing.apply_replacements_usec >= threshold_usec ||
			p_timing.validate_usec >= threshold_usec;
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

void export_timing_log_prescan_summary(const TransformOptions &p_options, const ExportPrescanTimingStats &p_timing) {
	if (!export_timing_should_log(p_options)) {
		return;
	}

	WARN_PRINT(export_timing_prefix() + vformat("prescan total=%s ms scripts=%d read=%s strip=%s deadcode=%s analyze=%s globals=%s builtins=%s paths=%s index_analyze=%s index=%s strings=%s",
			export_timing_format_msec(p_timing.total_usec),
			p_timing.script_count,
			export_timing_format_msec(p_timing.read_usec),
			export_timing_format_msec(p_timing.strip_usec),
			export_timing_format_msec(p_timing.deadcode_usec),
			export_timing_format_msec(p_timing.analyze_usec),
			export_timing_format_msec(p_timing.global_class_usec),
			export_timing_format_msec(p_timing.builtin_alias_usec),
			export_timing_format_msec(p_timing.path_obfuscation_usec),
			export_timing_format_msec(p_timing.index_analyze_usec),
			export_timing_format_msec(p_timing.index_script_usec),
			export_timing_format_msec(p_timing.string_resources_usec)));
}

void export_timing_log_transform_no_replacements(const TransformOptions &p_options, const String &p_path, const ExportTransformTimingStats &p_timing, const ExportReplacementTiming &p_replacement_timing, const RewriteContext &p_context, const ObfuscationNameProbe &p_name_probe) {
	if (!should_log_transform_timing(p_options, p_timing)) {
		return;
	}

	WARN_PRINT(export_timing_prefix() + vformat("transform %s total=%s ms strip=%s deadcode=%s analyze=%s setup=%s collect=%s collect_lines=%s collect_members=%s collect_nodes=%s local_name_calls=%s local_name=%s name_calls=%s name_time=%s string_literal_calls=%s string_literal=%s string_concat_calls=%s string_concat=%s collect_comments=%s collect_empty=%s overlap_checks=%s overlap_scans=%s overlap=%s replacements=0",
			p_path,
			export_timing_format_msec(p_timing.total_usec),
			export_timing_format_msec(p_timing.strip_usec),
			export_timing_format_msec(p_timing.deadcode_usec),
			export_timing_format_msec(p_timing.analyze_usec),
			export_timing_format_msec(p_timing.setup_usec),
			export_timing_format_msec(p_timing.collect_replacements_usec),
			export_timing_format_msec(p_replacement_timing.build_line_offsets_usec),
			export_timing_format_msec(p_replacement_timing.member_names_usec),
			export_timing_format_msec(p_replacement_timing.node_replacements_usec),
			String::num_uint64(p_context.local_name_make_calls),
			export_timing_format_msec(p_context.local_name_make_usec),
			String::num_uint64(p_name_probe.calls),
			export_timing_format_msec(p_name_probe.usec),
			String::num_uint64(p_context.string_literal_replacement_calls),
			export_timing_format_msec(p_context.string_literal_replacement_usec),
			String::num_uint64(p_context.string_concat_replacement_calls),
			export_timing_format_msec(p_context.string_concat_replacement_usec),
			export_timing_format_msec(p_replacement_timing.comment_replacements_usec),
			export_timing_format_msec(p_replacement_timing.empty_line_replacements_usec),
			String::num_uint64(p_context.overlap_check_count),
			String::num_uint64(p_context.overlap_scanned_replacements),
			export_timing_format_msec(p_context.overlap_check_usec)));
}

void export_timing_log_transform_with_replacements(const TransformOptions &p_options, const String &p_path, const ExportTransformTimingStats &p_timing, const ExportReplacementTiming &p_replacement_timing, const RewriteContext &p_context, const ObfuscationNameProbe &p_name_probe, int p_replacement_count) {
	if (!should_log_transform_timing(p_options, p_timing)) {
		return;
	}

	WARN_PRINT(export_timing_prefix() + vformat("transform %s total=%s ms strip=%s deadcode=%s analyze=%s setup=%s collect=%s collect_lines=%s collect_members=%s collect_nodes=%s local_name_calls=%s local_name=%s name_calls=%s name_time=%s string_literal_calls=%s string_literal=%s string_concat_calls=%s string_concat=%s collect_comments=%s collect_empty=%s overlap_checks=%s overlap_scans=%s overlap=%s apply=%s validate=%s replacements=%d",
			p_path,
			export_timing_format_msec(p_timing.total_usec),
			export_timing_format_msec(p_timing.strip_usec),
			export_timing_format_msec(p_timing.deadcode_usec),
			export_timing_format_msec(p_timing.analyze_usec),
			export_timing_format_msec(p_timing.setup_usec),
			export_timing_format_msec(p_timing.collect_replacements_usec),
			export_timing_format_msec(p_replacement_timing.build_line_offsets_usec),
			export_timing_format_msec(p_replacement_timing.member_names_usec),
			export_timing_format_msec(p_replacement_timing.node_replacements_usec),
			String::num_uint64(p_context.local_name_make_calls),
			export_timing_format_msec(p_context.local_name_make_usec),
			String::num_uint64(p_name_probe.calls),
			export_timing_format_msec(p_name_probe.usec),
			String::num_uint64(p_context.string_literal_replacement_calls),
			export_timing_format_msec(p_context.string_literal_replacement_usec),
			String::num_uint64(p_context.string_concat_replacement_calls),
			export_timing_format_msec(p_context.string_concat_replacement_usec),
			export_timing_format_msec(p_replacement_timing.comment_replacements_usec),
			export_timing_format_msec(p_replacement_timing.empty_line_replacements_usec),
			String::num_uint64(p_context.overlap_check_count),
			String::num_uint64(p_context.overlap_scanned_replacements),
			export_timing_format_msec(p_context.overlap_check_usec),
			export_timing_format_msec(p_timing.apply_replacements_usec),
			export_timing_format_msec(p_timing.validate_usec),
			p_replacement_count));
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
