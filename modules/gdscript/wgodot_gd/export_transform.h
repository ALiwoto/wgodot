// wgodot-changes::file
/**************************************************************************/
/*  export_transform.h                                                    */
/**************************************************************************/

#pragma once

#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

class Array;

namespace WGodotGDScriptExportTransform {

class ExportContext;

enum ObfuscationStrategy {
	OBFUSCATION_STRATEGY_SHORT,
	OBFUSCATION_STRATEGY_HASH,
	OBFUSCATION_STRATEGY_UNICODE,
};

struct TransformOptions {
	bool deconst_exports = true;
	bool obfuscate_names = false;
	bool obfuscate_builtin_names = true;
	bool obfuscate_file_paths = false;
	bool obfuscate_strings = false;
	bool redact_diagnostics = false;
	bool dead_code_injection_enabled = false;
	bool strip_comments = true;
	bool strip_empty_lines = true;
	bool binary_tokens_export = false;
	bool timing_logs_enabled = false;
	bool timing_verbose_logs_enabled = false;
	int min_in_class_dead_code_injection = 0;
	int max_in_class_dead_code_injection = 0;
	int max_dead_code_gaps_per_file = 5;
	int timing_slow_threshold_msec = 250;
	ObfuscationStrategy obfuscation_strategy = OBFUSCATION_STRATEGY_SHORT;
	ObfuscationStrategy file_path_obfuscation_strategy = OBFUSCATION_STRATEGY_SHORT;
};

void register_project_settings();
TransformOptions setup_params();
void transform_global_class_list(const ExportContext *p_context, Array *r_global_class_list);

} // namespace WGodotGDScriptExportTransform
