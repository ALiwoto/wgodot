// wgodot-changes::file
/**************************************************************************/
/*  export_timing.h                                                       */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "core/string/ustring.h"
#include "core/typedefs.h"

namespace WGodotGDScriptExportTransform {

struct RewriteContext;

struct ExportPrescanTimingStats {
	int script_count = 0;
	uint64_t total_usec = 0;
	uint64_t read_usec = 0;
	uint64_t strip_usec = 0;
	uint64_t deadcode_usec = 0;
	uint64_t analyze_usec = 0;
	uint64_t global_class_usec = 0;
	uint64_t builtin_alias_usec = 0;
	uint64_t path_obfuscation_usec = 0;
	uint64_t index_analyze_usec = 0;
	uint64_t index_script_usec = 0;
	uint64_t string_resources_usec = 0;
};

struct ObfuscationNameProbe {
	uint64_t calls = 0;
	uint64_t attempts = 0;
	uint64_t collisions = 0;
	uint64_t usec = 0;
	int max_attempts = 0;
};

struct ExportReplacementTiming {
	uint64_t build_line_offsets_usec = 0;
	uint64_t member_names_usec = 0;
	uint64_t node_replacements_usec = 0;
	uint64_t comment_replacements_usec = 0;
	uint64_t empty_line_replacements_usec = 0;
};

struct IndexScriptProbe {
	uint64_t classes = 0;
	uint64_t members = 0;
	uint64_t obfuscatable_members = 0;
	uint64_t reserve_script_global_usec = 0;
	uint64_t reserve_member_name_usec = 0;
	uint64_t member_get_name_usec = 0;
	uint64_t make_member_keys_usec = 0;
	uint64_t get_or_create_member_rename_usec = 0;
	uint64_t bind_member_rename_usec = 0;
};

struct ExportTransformTimingStats {
	uint64_t total_usec = 0;
	uint64_t strip_usec = 0;
	uint64_t deadcode_usec = 0;
	uint64_t analyze_usec = 0;
	uint64_t setup_usec = 0;
	uint64_t collect_replacements_usec = 0;
	uint64_t apply_replacements_usec = 0;
	uint64_t validate_usec = 0;
};

uint64_t export_timing_get_ticks_usec();
String export_timing_format_msec(uint64_t p_usec);
String export_timing_prefix();
bool export_timing_should_log(const TransformOptions &p_options);
bool export_timing_should_log_verbose(const TransformOptions &p_options);
uint64_t export_timing_slow_threshold_usec(const TransformOptions &p_options);
void export_timing_log_checkpoint(const TransformOptions &p_options, const String &p_context, const String &p_phase);
void export_timing_log_slow_phase(const TransformOptions &p_options, const String &p_context, const String &p_phase, uint64_t p_usec, uint64_t p_threshold_usec = 0);
void export_timing_log_prescan_summary(const TransformOptions &p_options, const ExportPrescanTimingStats &p_timing);
void export_timing_log_transform_no_replacements(const TransformOptions &p_options, const String &p_path, const ExportTransformTimingStats &p_timing, const ExportReplacementTiming &p_replacement_timing, const RewriteContext &p_context, const ObfuscationNameProbe &p_name_probe);
void export_timing_log_transform_with_replacements(const TransformOptions &p_options, const String &p_path, const ExportTransformTimingStats &p_timing, const ExportReplacementTiming &p_replacement_timing, const RewriteContext &p_context, const ObfuscationNameProbe &p_name_probe, int p_replacement_count);

void reset_obfuscation_name_probe();
ObfuscationNameProbe get_obfuscation_name_probe();
void set_obfuscation_name_probe_enabled(bool p_enabled);
bool is_obfuscation_name_probe_enabled();
void record_obfuscation_name_probe(int p_attempts, uint64_t p_usec);

} // namespace WGodotGDScriptExportTransform
