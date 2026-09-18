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
	bool obfuscate_strings = false;
	bool redact_diagnostics = false;
	bool strip_comments = true;
	bool strip_empty_lines = true;
	bool binary_tokens_export = false;
	bool timing_logs_enabled = false;
	bool timing_verbose_logs_enabled = false;
	int timing_slow_threshold_msec = 250;
	ObfuscationStrategy obfuscation_strategy = OBFUSCATION_STRATEGY_SHORT;
};

TransformOptions setup_params();
void transform_global_class_list(const ExportContext *p_context, Array *r_global_class_list);

} // namespace WGodotGDScriptExportTransform
